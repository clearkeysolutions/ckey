import CKeyScanKit
import CoreGraphics
import Foundation
#if canImport(SwiftUI)
import SwiftUI
#endif
#if canImport(UIKit) && !os(watchOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

/// What a view draws: a payload to encode, or an encoded symbol.
enum CKeyViewContent: Hashable {
    case payload(Data, version: Int, level: CKeyErrorCorrection, layout: CKeyLayout)
    case symbol(CKeySymbol)

    static func == (a: CKeyViewContent, b: CKeyViewContent) -> Bool {
        switch (a, b) {
        case let (.payload(d1, v1, l1, y1), .payload(d2, v2, l2, y2)):
            return d1 == d2 && v1 == v2 && l1 == l2 && y1 == y2
        case let (.symbol(s1), .symbol(s2)):
            return s1 === s2
        default:
            return false
        }
    }

    func hash(into hasher: inout Hasher) {
        switch self {
        case let .payload(data, version, level, layout):
            hasher.combine(0)
            hasher.combine(data)
            hasher.combine(version)
            hasher.combine(level)
            hasher.combine(layout)
        case let .symbol(symbol):
            hasher.combine(1)
            hasher.combine(ObjectIdentifier(symbol))
        }
    }

    func makeSymbol() throws -> CKeySymbol {
        switch self {
        case let .payload(data, version, level, layout):
            return try CKeySymbol.encode(data, version: version, level: level, layout: layout)
        case let .symbol(symbol):
            return symbol
        }
    }
}

/// Renders and caches the native images the views display, keyed by
/// content, pixel size and appearance.
final class CKeyImageCache {
    static let shared = CKeyImageCache()

    private struct Key: Hashable {
        let content: CKeyViewContent
        let pixels: Int
        let style: CKeyStyle
        let policy: CKeyRasterPolicy
        let transparent: Bool
    }

    private final class Box: NSObject {
        let key: Key
        init(_ key: Key) { self.key = key }
        override var hash: Int { key.hashValue }
        override func isEqual(_ object: Any?) -> Bool { (object as? Box)?.key == key }
    }

    private let cache: NSCache<Box, CGImage> = {
        let cache = NSCache<Box, CGImage>()
        cache.countLimit = 32
        return cache
    }()

    func image(_ content: CKeyViewContent, pixels: Int, style: CKeyStyle, policy: CKeyRasterPolicy,
               transparent: Bool) -> CGImage? {
        guard pixels >= 16 else { return nil }
        let box = Box(Key(content: content, pixels: min(pixels, 16384), style: style, policy: policy, transparent: transparent))
        if let hit = cache.object(forKey: box) { return hit }
        // Pixel-perfect: the finest lattice that fits (most cells per module),
        // drawn at one image pixel per display pixel (the views centre it and
        // never scale it). `policy` applies only to `CKeySymbol.cgImage`.
        guard let symbol = try? content.makeSymbol(),
              let fit = try? symbol.pixelPerfectFit(pixels: box.key.pixels),
              let image = try? symbol.pixelPerfectImage(pxPerCell: fit.pxPerCell, cellsPerModule: fit.cellsPerModule,
                                                        transparent: transparent, style: style)
        else { return nil }
        cache.setObject(image, forKey: box)
        return image
    }
}

#if canImport(SwiftUI)
/// A SwiftUI view that draws a CKey pixel-perfect: the core's lattice, with
/// the most whole display pixels per cell that fit the view's pixel size
/// (points x display scale), shown unscaled and centred; no SVG, file or web
/// view is involved. A view smaller than the lattice at one pixel per cell
/// shows it at one pixel per cell, cropped by the view's bounds. The view is square
/// (aspect ratio 1) and transparent by default: no background, border or
/// chrome. With `transparent: true` the host must place it on a background
/// that contrasts with the style's ink (black for `.plain`).
///
///     CKeyView(data: "ABC123")
///         .frame(width: 160, height: 160)
public struct CKeyView: View {
    private let content: CKeyViewContent
    private let style: CKeyStyle
    private let transparent: Bool
    private let policy: CKeyRasterPolicy
    private let label: String
    @Environment(\.displayScale) private var displayScale

    /// Encodes UTF-8 text as a Profile 0 symbol.
    public init(data: String, version: Int = 0, level: CKeyErrorCorrection = .m, layout: CKeyLayout = .standard,
                style: CKeyStyle = .plain, transparent: Bool = true, policy: CKeyRasterPolicy = .coverage,
                accessibilityLabel: String = "CKey code") {
        self.init(bytes: Data(data.utf8), version: version, level: level, layout: layout, style: style,
                  transparent: transparent, policy: policy, accessibilityLabel: accessibilityLabel)
    }

    /// Encodes bytes as a Profile 0 symbol.
    public init(bytes: Data, version: Int = 0, level: CKeyErrorCorrection = .m, layout: CKeyLayout = .standard,
                style: CKeyStyle = .plain, transparent: Bool = true, policy: CKeyRasterPolicy = .coverage,
                accessibilityLabel: String = "CKey code") {
        content = .payload(bytes, version: version, level: level, layout: layout)
        self.style = style
        self.transparent = transparent
        self.policy = policy
        label = accessibilityLabel
    }

    /// Draws an already encoded symbol.
    public init(symbol: CKeySymbol, style: CKeyStyle = .plain, transparent: Bool = true,
                policy: CKeyRasterPolicy = .coverage, accessibilityLabel: String = "CKey code") {
        content = .symbol(symbol)
        self.style = style
        self.transparent = transparent
        self.policy = policy
        label = accessibilityLabel
    }

    public var body: some View {
        GeometryReader { proxy in
            let side = max(0, min(proxy.size.width, proxy.size.height))
            let scale = max(displayScale, 1)
            let pixels = Int((side * scale).rounded(.down))
            ZStack {
                if let image = CKeyImageCache.shared.image(content, pixels: pixels, style: style,
                                                           policy: policy, transparent: transparent) {
                    Image(decorative: image, scale: scale, orientation: .up)
                        .interpolation(.none)
                        .antialiased(false)
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height)
        }
        .aspectRatio(1, contentMode: .fit)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text(label))
        .accessibilityAddTraits(.isImage)
    }
}
#endif

#if canImport(UIKit) && !os(watchOS)
/// A UIKit view that draws a CKey as native pixels at its exact pixel size
/// (bounds x `traitCollection.displayScale`). Transparent by default.
open class CKeyUIView: UIView {
    /// The symbol to draw; `nil` draws nothing.
    public var symbol: CKeySymbol? { didSet { setNeedsLayout() } }
    public var style: CKeyStyle = .plain { didSet { setNeedsLayout() } }
    public var transparent: Bool = true { didSet { setNeedsLayout() } }
    public var policy: CKeyRasterPolicy = .coverage { didSet { setNeedsLayout() } }

    public override init(frame: CGRect) {
        super.init(frame: frame)
        setUp()
    }

    public required init?(coder: NSCoder) {
        super.init(coder: coder)
        setUp()
    }

    /// Encodes `data` (UTF-8). A payload that cannot be encoded draws nothing.
    public convenience init(data: String, version: Int = 0, level: CKeyErrorCorrection = .m,
                            layout: CKeyLayout = .standard) {
        self.init(frame: .zero)
        symbol = try? CKeySymbol.encode(data, version: version, level: level, layout: layout)
    }

    private func setUp() {
        isOpaque = false
        backgroundColor = .clear
        layer.contentsGravity = .center
        layer.magnificationFilter = .nearest
        isAccessibilityElement = true
        accessibilityLabel = "CKey code"
        accessibilityTraits = .image
    }

    open override func layoutSubviews() {
        super.layoutSubviews()
        let scale = max(traitCollection.displayScale, 1)
        let side = min(bounds.width, bounds.height)
        let pixels = Int((side * scale).rounded(.down))
        layer.contentsScale = scale
        guard let symbol else { layer.contents = nil; return }
        layer.contents = CKeyImageCache.shared.image(.symbol(symbol), pixels: pixels, style: style,
                                                     policy: policy, transparent: transparent)
    }

    open override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous)
        if previous?.displayScale != traitCollection.displayScale { setNeedsLayout() }
    }
}
#elseif os(macOS)
/// An AppKit view that draws a CKey as native pixels at its exact pixel size
/// (bounds x the window's backing scale factor). Transparent by default.
open class CKeyNSView: NSView {
    /// The symbol to draw; `nil` draws nothing.
    public var symbol: CKeySymbol? { didSet { needsLayout = true } }
    public var style: CKeyStyle = .plain { didSet { needsLayout = true } }
    public var transparent: Bool = true { didSet { needsLayout = true } }
    public var policy: CKeyRasterPolicy = .coverage { didSet { needsLayout = true } }

    public override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setUp()
    }

    public required init?(coder: NSCoder) {
        super.init(coder: coder)
        setUp()
    }

    /// Encodes `data` (UTF-8). A payload that cannot be encoded draws nothing.
    public convenience init(data: String, version: Int = 0, level: CKeyErrorCorrection = .m,
                            layout: CKeyLayout = .standard) {
        self.init(frame: .zero)
        symbol = try? CKeySymbol.encode(data, version: version, level: level, layout: layout)
    }

    private func setUp() {
        wantsLayer = true
        layer?.contentsGravity = .center
        layer?.magnificationFilter = .nearest
        setAccessibilityElement(true)
        setAccessibilityRole(.image)
        setAccessibilityLabel("CKey code")
    }

    open override func layout() {
        super.layout()
        let scale = max(window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 1, 1)
        let side = min(bounds.width, bounds.height)
        let pixels = Int((side * scale).rounded(.down))
        layer?.contentsScale = scale
        guard let symbol else { layer?.contents = nil; return }
        layer?.contents = CKeyImageCache.shared.image(.symbol(symbol), pixels: pixels, style: style,
                                                      policy: policy, transparent: transparent)
    }

    open override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        needsLayout = true
    }
}
#endif
