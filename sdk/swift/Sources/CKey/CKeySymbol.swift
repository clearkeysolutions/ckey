import CKeyCodec
import CKeyScanKit
import CoreGraphics
import Foundation

/// Reed-Solomon correction level for Profile 0.
public enum CKeyErrorCorrection: Int, Sendable, CaseIterable {
    /// About 7% of each block correctable.
    case l = 0
    /// About 14% (default).
    case m = 1
    /// About 22%.
    case q = 2
    /// About 30%.
    case h = 3
    /// Ultra: about 40% (80% as erasures). Carries about half of `.h`'s
    /// payload. Readers before 0.1.0-build.86 reject it.
    case u = 4
    /// Extreme: about 47.5% (95% as erasures). Carries about 5% of `.l`'s
    /// payload, and nothing at all in the smallest versions
    /// (`CKeySymbol.capacity` returns 0). Readers before 0.1.0-build.86
    /// reject it.
    case x = 5

    var c: ckey_ecc_level { ckey_ecc_level(rawValue: UInt32(rawValue)) }
}

/// How the direct rasterizer turns geometry into pixels (specification §2.2).
/// None is universally best; benchmark for your target size.
public enum CKeyRasterPolicy: Int, Sendable, CaseIterable {
    /// Antialiased area coverage (default).
    case coverage = 0
    /// Each pixel is ink when at least half its samples are covered.
    case binary = 1
    /// Coverage, with marks thinner than one pixel widened to keep thin rays
    /// and dots continuous. Changes drawn widths, never the encoded topology.
    case hybrid = 2
}

/// A straight-alpha (non-premultiplied) sRGB RGBA8 pixel buffer, rows top to
/// bottom with no padding: what the core's raster backend produces.
public struct CKeyPixels: Sendable {
    public let width: Int
    public let height: Int
    /// `width * height * 4` bytes.
    public let rgba: Data
    /// `false` when the background was painted (every alpha is 255).
    public let hasAlpha: Bool
}

/// An encoded Profile 0 (scalable family) symbol. Immutable; renders to
/// native pixels (`cgImage`, `pixels`), PNG, SVG and PDF. Thread-safe.
public final class CKeySymbol: @unchecked Sendable {
    // ckey_scalable_symbol is 8 KB; keep one heap copy, never copied by value.
    private let storage: UnsafeMutablePointer<ckey_scalable_symbol>

    /// The version actually used (1...31); `version: 0` picks the smallest that fits.
    public let version: Int
    public let level: CKeyErrorCorrection
    public let layout: CKeyLayout
    /// The payload size in bytes.
    public let payloadLength: Int
    /// Modules across the field for this version (29...232).
    public var modulesAcross: Int { Int(ckey_scalable_modules_across(UInt32(version))) }

    private init(storage: UnsafeMutablePointer<ckey_scalable_symbol>) {
        self.storage = storage
        version = Int(storage.pointee.version)
        level = CKeyErrorCorrection(rawValue: Int(storage.pointee.ecc_level)) ?? .m
        layout = CKeyLayout(rawValue: Int(storage.pointee.layout)) ?? .standard
        payloadLength = Int(storage.pointee.payload_length)
    }

    deinit {
        storage.deinitialize(count: 1)
        storage.deallocate()
    }

    /// Encodes UTF-8 text.
    /// - Parameters:
    ///   - version: 1...31, or 0 (default) for the smallest version that fits.
    ///   - level: correction level (default `.m`); `.u` and `.x` trade most
    ///     of the capacity for parity.
    ///   - layout: `.standard` (default), `.print` (P0.1, for small prints), or
    ///     their byte-grouped forms `.standardGrouped` and `.printGrouped`.
    /// - Throws: `CKeyError` when the payload does not fit or an argument is invalid.
    public static func encode(_ text: String, version: Int = 0, level: CKeyErrorCorrection = .m,
                              layout: CKeyLayout = .standard) throws -> CKeySymbol {
        try encode(Data(text.utf8), version: version, level: level, layout: layout)
    }

    /// Encodes bytes. See `encode(_:version:level:layout:)` for the parameters.
    public static func encode(_ data: Data, version: Int = 0, level: CKeyErrorCorrection = .m,
                              layout: CKeyLayout = .standard) throws -> CKeySymbol {
        guard (0...Int(CKEY_SCALABLE_VERSION_COUNT)).contains(version) else {
            throw CKeyError(.invalidArgument, "version must be 0 (auto) or 1...\(CKEY_SCALABLE_VERSION_COUNT)")
        }
        let storage = UnsafeMutablePointer<ckey_scalable_symbol>.allocate(capacity: 1)
        storage.initialize(to: ckey_scalable_symbol())
        var diagnostics = ckey_diagnostics()
        let bytes = [UInt8](data)
        let status = bytes.withUnsafeBufferPointer { payload in
            ckey_encode_scalable(payload.baseAddress, payload.count, UInt32(version), level.c,
                                 ckey_scalable_layout(rawValue: UInt32(layout.rawValue)), storage, &diagnostics)
        }
        guard status == CKEY_OK else {
            storage.deinitialize(count: 1)
            storage.deallocate()
            throw CKeyError.core(status, diagnostics, "encoding failed")
        }
        return CKeySymbol(storage: storage)
    }

    /// Maximum payload bytes for a version (1...31), level and layout; 0 if invalid.
    public static func capacity(version: Int, level: CKeyErrorCorrection = .m, layout: CKeyLayout = .standard) -> Int {
        guard (1...Int(CKEY_SCALABLE_VERSION_COUNT)).contains(version) else { return 0 }
        return ckey_scalable_capacity(UInt32(version), level.c, ckey_scalable_layout(rawValue: UInt32(layout.rawValue)))
    }

    // MARK: - Native pixels

    /// Renders straight-alpha RGBA8 pixels covering the full 31-unit canvas
    /// (symbol plus quiet margin).
    /// - Parameters:
    ///   - size: pixels per side, 16...16384.
    ///   - policy: raster policy (default `.coverage`).
    ///   - transparent: paint no background, vignette or texture (unpainted
    ///     pixels have alpha 0). The host is then responsible for contrast.
    ///   - style: the design (default `.plain`).
    ///   - materials: draw sheen, depth and grain (default `true`); `false` draws flat ink.
    public func pixels(size: Int = 512, policy: CKeyRasterPolicy = .coverage, transparent: Bool = false,
                       style: CKeyStyle = .plain, materials: Bool = true) throws -> CKeyPixels {
        guard (16...16384).contains(size) else { throw CKeyError(.invalidArgument, "size must be 16...16384 px") }
        var options = ckey_raster_options()
        ckey_raster_options_default(&options)
        options.size_px = UInt32(size)
        options.policy = UInt32(policy.rawValue)
        options.format = CKEY_PIXELS_RGBA8.rawValue
        options.transparent = transparent ? 1 : 0
        options.materials = materials ? 1 : 0
        let symbol = UnsafePointer(storage)
        let data: Data = try style.withCStyle { cStyle -> Data in
            var required = 0
            let probe = ckey_render_scalable_raster(symbol, cStyle, &options, nil, 0, &required)
            guard probe == CKEY_OK || probe == CKEY_INSUFFICIENT_BUFFER, required > 0 else {
                throw refusal(probe, symbol, cStyle, "the style was refused or the raster options are invalid")
            }
            var buffer = Data(count: required)
            let status = buffer.withUnsafeMutableBytes { raw in
                ckey_render_scalable_raster(symbol, cStyle, &options,
                                            raw.bindMemory(to: UInt8.self).baseAddress, raw.count, &required)
            }
            guard status == CKEY_OK else { throw CKeyError.core(status, nil, "rendering failed") }
            return buffer
        }
        return CKeyPixels(width: size, height: size, rgba: data, hasAlpha: transparent)
    }

    /// Renders a `CGImage` (sRGB, 8 bits per channel). For on-screen use,
    /// pass the display's pixel size, not points (`CKeyView` does this).
    /// See `pixels(size:policy:transparent:style:materials:)` for the parameters.
    public func cgImage(size: Int = 512, policy: CKeyRasterPolicy = .coverage, transparent: Bool = false,
                        style: CKeyStyle = .plain, materials: Bool = true) throws -> CGImage {
        let rendered = try pixels(size: size, policy: policy, transparent: transparent, style: style, materials: materials)
        let alpha: CGImageAlphaInfo = rendered.hasAlpha ? .last : .noneSkipLast
        guard let provider = CGDataProvider(data: rendered.rgba as CFData),
              let space = CGColorSpace(name: CGColorSpace.sRGB),
              let image = CGImage(width: rendered.width, height: rendered.height,
                                  bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: rendered.width * 4,
                                  space: space, bitmapInfo: CGBitmapInfo(rawValue: alpha.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false,
                                  intent: .defaultIntent) else {
            throw CKeyError(.imageConversion, "could not create a CGImage")
        }
        return image
    }

    // MARK: - Exports

    /// PNG bytes of the raster (lossless; written by the core, no ImageIO).
    /// - Parameter dpi: when > 0, records the physical resolution (pHYs).
    public func pngData(size: Int = 512, policy: CKeyRasterPolicy = .coverage, transparent: Bool = false,
                        style: CKeyStyle = .plain, materials: Bool = true, dpi: Double = 0) throws -> Data {
        let rendered = try pixels(size: size, policy: policy, transparent: transparent, style: style, materials: materials)
        return try rendered.rgba.withUnsafeBytes { raw -> Data in
            let pixels = raw.bindMemory(to: UInt8.self).baseAddress
            let width = UInt32(rendered.width), height = UInt32(rendered.height), stride = rendered.width * 4
            var required = 0
            let probe = ckey_encode_png(pixels, width, height, stride, CKEY_PNG_RGBA8, dpi, nil, 0, &required)
            guard probe == CKEY_OK || probe == CKEY_INSUFFICIENT_BUFFER, required > 0 else {
                throw CKeyError.core(probe, nil, "PNG encoding failed")
            }
            var png = Data(count: required)
            let status = png.withUnsafeMutableBytes { out in
                ckey_encode_png(pixels, width, height, stride, CKEY_PNG_RGBA8, dpi,
                                out.bindMemory(to: UInt8.self).baseAddress, out.count, &required)
            }
            guard status == CKEY_OK else { throw CKeyError.core(status, nil, "PNG encoding failed") }
            return png.prefix(required)
        }
    }

    /// Styled SVG export (vector; for interchange and print workflows, not
    /// for on-screen display, which uses native pixels).
    /// - Parameter scale: pixels per canonical unit in the SVG's size attributes (1...256).
    public func svg(scale: Int = 8, style: CKeyStyle = .plain) throws -> String {
        guard (1...Int(CKEY_MAX_RENDER_SCALE)).contains(scale) else {
            throw CKeyError(.invalidArgument, "scale must be 1...\(CKEY_MAX_RENDER_SCALE)")
        }
        let symbol = UnsafePointer(storage)
        return try style.withCStyle { cStyle -> String in
            var required = 0
            let probe = ckey_render_scalable_styled_svg(symbol, cStyle, UInt32(scale), nil, 0, &required)
            guard probe == CKEY_OK || probe == CKEY_INSUFFICIENT_BUFFER, required > 0 else {
                throw refusal(probe, symbol, cStyle, "the style was refused")
            }
            var chars = [CChar](repeating: 0, count: required + 1)
            let status = chars.withUnsafeMutableBufferPointer { buffer in
                ckey_render_scalable_styled_svg(symbol, cStyle, UInt32(scale), buffer.baseAddress, buffer.count, &required)
            }
            guard status == CKEY_OK else { throw CKeyError.core(status, nil, "SVG export failed") }
            chars[chars.count - 1] = 0
            return String(cString: chars)
        }
    }

    /// One-page PDF 1.4 with the exact geometry as vector paths (flat
    /// colours; materials and logos are not exported).
    /// - Parameter widthPoints: page width in points (72 per inch); default 2 inches.
    public func pdfData(widthPoints: Double = 144, transparent: Bool = false, style: CKeyStyle = .plain) throws -> Data {
        guard widthPoints.isFinite, widthPoints > 0 else { throw CKeyError(.invalidArgument, "widthPoints must be positive") }
        let symbol = UnsafePointer(storage)
        return try style.withCStyle { cStyle -> Data in
            var required = 0
            let flag: UInt32 = transparent ? 1 : 0
            let probe = ckey_render_scalable_pdf(symbol, cStyle, widthPoints, flag, nil, 0, &required)
            guard probe == CKEY_OK || probe == CKEY_INSUFFICIENT_BUFFER, required > 0 else {
                throw refusal(probe, symbol, cStyle, "the style was refused")
            }
            var pdf = Data(count: required)
            let status = pdf.withUnsafeMutableBytes { out in
                ckey_render_scalable_pdf(symbol, cStyle, widthPoints, flag,
                                         out.bindMemory(to: UInt8.self).baseAddress, out.count, &required)
            }
            guard status == CKEY_OK else { throw CKeyError.core(status, nil, "PDF export failed") }
            return pdf.prefix(required)
        }
    }

    // MARK: - Style check and certification

    /// Checks a style against this symbol (contrast, effect limits, logo
    /// error budget). Throws a `CKeyError` explaining any refusal.
    public func check(style: CKeyStyle) throws {
        let symbol = UnsafePointer(storage)
        try style.withCStyle { cStyle -> Void in
            var diagnostics = ckey_diagnostics()
            let status = ckey_check_scalable_style(symbol, cStyle, &diagnostics)
            guard status == CKEY_OK else { throw CKeyError.core(status, diagnostics, "style refused") }
        }
    }

    /// Certifies a style by rendering it, compositing it over each backdrop,
    /// degrading it with the default phone-camera model and decoding it with
    /// the real decoder (`ckey_certify_scalable_style`), under `style.policy`.
    /// A model is not a measurement: real-camera tests remain the evidence.
    ///
    /// - Parameters:
    ///   - transparent: certify transparent output. It needs `backdrops`
    ///     (the colours the symbol may sit on); without them the result is
    ///     never certified ("background unknown").
    ///   - backdrops: up to 16 solid backgrounds.
    public func certify(style: CKeyStyle = .plain, transparent: Bool = false,
                        backdrops: [CKeyColor] = []) throws -> CKeyCertification {
        guard backdrops.count <= 16 else { throw CKeyError(.invalidArgument, "at most 16 backdrop colours") }
        let symbol = UnsafePointer(storage)
        let colours = backdrops.map(\.c)
        return style.withCStyle { cStyle -> CKeyCertification in
            var adapted = ckey_style()
            var report = ckey_certification()
            let status: ckey_status = colours.withUnsafeBufferPointer { list in
                if colours.isEmpty {
                    return ckey_certify_scalable_style(symbol, cStyle, transparent ? 1 : 0, nil, nil, &adapted, &report)
                }
                var backdrop = ckey_backdrop()
                backdrop.colours = list.baseAddress
                backdrop.colour_count = UInt32(list.count)
                return ckey_certify_scalable_style(symbol, cStyle, transparent ? 1 : 0, &backdrop, nil, &adapted, &report)
            }
            return CKeyCertification(status: status, report: report,
                                     adapted: status == CKEY_OK && style.policy == .adaptive ? adapted : nil,
                                     logoHref: style.logoHref)
        }
    }
}

/// A renderer's refusal, explained by the style check when the style is the cause.
private func refusal(_ status: ckey_status, _ symbol: UnsafePointer<ckey_scalable_symbol>,
                     _ style: UnsafePointer<ckey_style>, _ fallback: String) -> CKeyError {
    var diagnostics = ckey_diagnostics()
    if status == CKEY_INVALID_ARGUMENT, ckey_check_scalable_style(symbol, style, &diagnostics) != CKEY_OK {
        return CKeyError.core(status, diagnostics, fallback)
    }
    return CKeyError.core(status, nil, fallback)
}

/// The outcome of `CKeySymbol.certify`.
public struct CKeyCertification: Sendable {
    /// Changes made under the `.adaptive` policy (`CKEY_ADJUST_*`).
    public struct Adjustments: OptionSet, Sendable {
        public let rawValue: UInt32
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        public static let ink = Adjustments(rawValue: 1)
        public static let accents = Adjustments(rawValue: 2)
        public static let effects = Adjustments(rawValue: 4)
        public static let logo = Adjustments(rawValue: 8)
        public static let background = Adjustments(rawValue: 16)
        public static let materials = Adjustments(rawValue: 32)
    }

    /// The core accepted the style under its policy (`.strict` requires
    /// certification; `.adaptive` succeeds once repaired; `.override` always).
    public let accepted: Bool
    /// The symbol decoded in every modelled trial on every backdrop.
    public let certified: Bool
    public let policy: CKeyStylePolicy
    public let adjustments: Adjustments
    public let trials: Int
    public let trialsPassed: Int
    /// Informative WCAG-style contrast ratio over all ink/background pairs.
    public let minContrast: Double
    /// Luma separation of marks versus gaps after compositing (<= 0: they overlap).
    public let featureSeparation: Double
    /// Share of the worst block's correction hidden by the logo.
    public let logoLoad: Double
    /// The reason for a refusal, or the list of changes and warnings.
    public let message: String
    /// Under `.adaptive`, the repaired style to render.
    public let adaptedStyle: CKeyStyle?

    init(status: ckey_status, report: ckey_certification, adapted: ckey_style?, logoHref: String?) {
        accepted = status == CKEY_OK
        certified = report.certified != 0
        policy = CKeyStylePolicy(rawValue: Int(report.policy)) ?? .strict
        adjustments = Adjustments(rawValue: report.adjustments)
        trials = Int(report.trials)
        trialsPassed = Int(report.trials_passed)
        minContrast = report.min_contrast
        featureSeparation = report.feature_separation
        logoLoad = report.logo_load
        message = withUnsafeBytes(of: report.message) { raw in
            let bytes = raw.bindMemory(to: UInt8.self)
            let end = bytes.firstIndex(of: 0) ?? bytes.count
            return String(decoding: bytes[0..<end], as: UTF8.self)
        }
        // The adapted style's logo pointer refers to the caller's C string,
        // which is only valid inside the call: never read it, restore the
        // original logo instead.
        if var style = adapted {
            style.logo_href = nil
            var repaired = CKeyStyle(style)
            repaired.logoHref = logoHref
            adaptedStyle = repaired
        } else {
            adaptedStyle = nil
        }
    }
}
