import CKeyCodec
import CoreGraphics
import CoreVideo
import Foundation
#if canImport(ImageIO)
import ImageIO
#endif

/// What one scan of a frame found: the decoded symbol, if any, and where a
/// symbol-like candidate sits even when it did not decode. Camera layers use
/// the candidate to aim focus and exposure, to seed tracking and to tell the
/// user what to change.
public struct CKeyScanOutcome: Equatable, Sendable {
    /// The decoded symbol; `nil` when nothing decoded.
    public let result: CKeyResult?
    /// True when `quad` holds a symbol-like candidate, decoded or not.
    public let located: Bool
    /// The candidate's four field corners in full-frame pixels (x right, y
    /// down), in the symbol's own order when it decoded. Empty unless `located`.
    public let quad: [CGPoint]
    /// The fit score of `quad` against the CKey keys (higher is better; whole
    /// codes score in the hundreds on clean renders, other rectangles 0).
    public let score: Double
    /// A symbol-like border runs off the frame or the region: the user
    /// should move back.
    public let symbolCut: Bool
    /// The scan stopped at its work budget before trying everything.
    public let budgetExhausted: Bool
    /// Reed-Solomon symbols corrected in the decode (0 without one).
    public let correctedSymbols: Int
    /// Bytes treated as erasures (glare, occlusion, sites outside the image)
    /// in the decode that succeeded.
    public let erasuresUsed: Int
    /// Border candidates found in the frame or region.
    public let candidates: Int
    /// The frame's size in pixels.
    public let frameSize: CGSize
    /// True when the payload came from several frames averaged together
    /// (multi-frame reading, `CKeyFrameProcessor.multiFrame`), none of which
    /// decoded alone.
    public internal(set) var accumulated = false

    /// `quad` as fractions of the frame (0...1): for a camera frame in the
    /// sensor's native orientation these are AVFoundation's capture-device
    /// coordinates.
    public var normalizedQuad: [CGPoint] {
        guard frameSize.width > 0, frameSize.height > 0 else { return [] }
        return quad.map { CGPoint(x: $0.x / frameSize.width, y: $0.y / frameSize.height) }
    }

    static func nothing(width: Int, height: Int) -> CKeyScanOutcome {
        CKeyScanOutcome(result: nil, located: false, quad: [], score: 0, symbolCut: false, budgetExhausted: false,
                        correctedSymbols: 0, erasuresUsed: 0, candidates: 0,
                        frameSize: CGSize(width: max(0, width), height: max(0, height)))
    }
}

/// Headless decoding of still images and camera frames.
///
/// Every entry point is one call into the C++ core
/// (`ckey_scan_frame_gray8`): border search (both polarities), refinement
/// and decoding for Profile 0 first, then Profiles 1, 2 and 3, each against
/// its own keys. A payload is returned only after the core's Reed-Solomon
/// correction and CRC-32C checks accept it. With a region of interest only
/// that rectangle is searched, at full resolution and in place.
///
/// All functions are synchronous, reentrant and safe to call from any
/// thread; call them off the main thread for camera-sized frames.
public enum CKeyDecoder {
    /// Maximum border candidates tried per frame.
    public static let maxCandidates = 6
    /// The margin `pixelRegion` adds around a region, as a fraction of its
    /// size on each side, so a code that overhangs it is still whole (codes
    /// held close are often larger than the reticle).
    public static let regionMargin: CGFloat = 0.35

    // MARK: - Decode (payload only)

    /// Decodes an 8-bit grayscale frame (`stride` bytes per row). Returns
    /// `nil` when no symbol decodes.
    /// - Parameter region: an optional region of interest in pixels; only it
    ///   is searched. `nil` (default) searches the whole frame.
    public static func decode(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                              region: CGRect? = nil) -> CKeyResult? {
        scan(gray: gray, width: width, height: height, stride: stride, region: region).result
    }

    /// Decodes an image. It is drawn over white into an 8-bit gray buffer,
    /// so a transparent rendering (dark ink, alpha 0 elsewhere) reads as
    /// dark-on-white. Returns `nil` when no symbol decodes.
    public static func decode(cgImage image: CGImage) -> CKeyResult? {
        scan(cgImage: image)?.result
    }

    /// Decodes a camera or video frame. Supported formats: the bi-planar
    /// 4:2:0 YpCbCr formats (luma plane, as AVFoundation delivers by
    /// default), 8-bit one-component gray, and 32-bit BGRA/RGBA/ARGB (converted
    /// to luma). Returns `nil` when no symbol decodes or the format is unsupported.
    /// - Parameter normalizedRegion: an optional region of interest as
    ///   fractions of the frame (0...1); `regionMargin` is added around it.
    public static func decode(pixelBuffer: CVPixelBuffer, normalizedRegion: CGRect? = nil) -> CKeyResult? {
        scan(pixelBuffer: pixelBuffer, normalizedRegion: normalizedRegion)?.result
    }

    // MARK: - Scan (payload, candidate and flags)

    /// Scans an 8-bit grayscale frame (`stride` bytes per row).
    /// - Parameter region: an optional region of interest in pixels (see
    ///   `pixelRegion`). Only it is searched, in place; coordinates in the
    ///   outcome are always full-frame. Alternate region scans with whole
    ///   frames so symbols outside the region still read.
    public static func scan(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                            region: CGRect? = nil) -> CKeyScanOutcome {
        // Profile 0 payloads reach 4,619 bytes (v31, level M): 64 KB output.
        var payload = [UInt8](repeating: 0, count: payloadCapacity)
        return scan(gray: gray, width: width, height: height, stride: stride, region: region, payload: &payload)
    }

    #if canImport(ImageIO)
    /// Decodes a photo or screenshot file (JPEG, HEIC, PNG...). The image is
    /// read with its orientation applied and at most 4,096 pixels on its long
    /// side, then, if that finds nothing, at 2,048 (a smaller image can read
    /// better when the code is large and soft). Returns `nil` when no symbol
    /// decodes or the data is not an image.
    public static func decode(imageData data: Data) -> CKeyResult? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil) else { return nil }
        for maxSide in [4096, 2048] {
            let options: [CFString: Any] = [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceShouldCacheImmediately: true,
                kCGImageSourceThumbnailMaxPixelSize: maxSide,
            ]
            guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else { continue }
            if let result = decode(cgImage: image) { return result }
            if max(image.width, image.height) < maxSide { break }  // already at full size
        }
        return nil
    }
    #endif

    /// Scans an image drawn over white (see `decode(cgImage:)`). Returns
    /// `nil` when the image cannot be drawn.
    public static func scan(cgImage image: CGImage) -> CKeyScanOutcome? {
        guard let frame = GrayFrame(image) else { return nil }
        return frame.bytes.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return nil }
            return scan(gray: base, width: frame.width, height: frame.height, stride: frame.width)
        }
    }

    /// Scans a camera or video frame (formats as `decode(pixelBuffer:)`).
    /// Returns `nil` when the pixel format is unsupported.
    /// - Parameter normalizedRegion: an optional region of interest as
    ///   fractions of the frame (0...1); `regionMargin` is added around it.
    public static func scan(pixelBuffer: CVPixelBuffer, normalizedRegion: CGRect? = nil) -> CKeyScanOutcome? {
        CKeyLuma.withGray(pixelBuffer) { gray, width, height, stride -> CKeyScanOutcome in
            let region = normalizedRegion.flatMap { pixelRegion(normalized: $0, width: width, height: height) }
            return scan(gray: gray, width: width, height: height, stride: stride, region: region)
        }
    }

    /// A normalized region (fractions of the frame) grown by `margin` on each
    /// side, in whole pixels and clamped to the frame; `nil` when the result
    /// would be under 64 pixels on a side, too small to hold a code.
    public static func pixelRegion(normalized region: CGRect, margin: CGFloat = regionMargin,
                                   width: Int, height: Int) -> CGRect? {
        guard width > 0, height > 0, !region.isNull, !region.isInfinite else { return nil }
        let standard = region.standardized
        guard standard.width.isFinite, standard.height.isFinite, standard.minX.isFinite, standard.minY.isFinite else {
            return nil
        }
        let grown = standard.insetBy(dx: -standard.width * margin, dy: -standard.height * margin)
            .intersection(CGRect(x: 0, y: 0, width: 1, height: 1))
        guard !grown.isNull else { return nil }
        let x0 = max(0, Int((grown.minX * CGFloat(width)).rounded(.down)))
        let y0 = max(0, Int((grown.minY * CGFloat(height)).rounded(.down)))
        let x1 = min(width, Int((grown.maxX * CGFloat(width)).rounded(.up)))
        let y1 = min(height, Int((grown.maxY * CGFloat(height)).rounded(.up)))
        guard x1 - x0 >= 64, y1 - y0 >= 64 else { return nil }
        return CGRect(x: x0, y: y0, width: x1 - x0, height: y1 - y0)
    }

    // MARK: - Machine learning

    /// Turns the core's built-in ML assistance on or off for the whole
    /// process (default on): the locator (tried after the classical border
    /// search fails), the corner refiner (after a failed decode) and the site
    /// model (after the plain reading fails). Off, scanning is exactly the
    /// classical reader. A payload is accepted only after Reed-Solomon and
    /// CRC-32C either way.
    public static func setMachineLearning(enabled: Bool) {
        if enabled { _ = ckey_ml_load_model(nil, 0) } else { ckey_ml_clear_models() }
    }

    /// True while any ML model is active.
    public static var machineLearningEnabled: Bool {
        ckey_ml_model_id() != 0 || ckey_ml_site_model_id() != 0 || ckey_ml_refiner_model_id() != 0
    }

    // MARK: - Core call

    static let payloadCapacity = 65535

    /// The scan with a caller-owned payload buffer (camera paths reuse one).
    /// With an `accumulator` (`ckey_accumulator_create`), a frame that does not
    /// decode is averaged with the previous ones of the same symbol and the
    /// average is decoded (`ckey_scan_frame_accumulate_gray8`).
    /// With `fast` (live camera frames), the classical reader only, two
    /// candidates and two version hypotheses (`CKEY_SCAN_OPTION_FAST`): a frame
    /// that will not read gives up quickly so the next one is looked at.
    static func scan(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int, region: CGRect?,
                     payload: inout [UInt8], accumulator: OpaquePointer? = nil, fast: Bool = false) -> CKeyScanOutcome {
        guard width >= 16, height >= 16, stride >= width, !payload.isEmpty else {
            return .nothing(width: width, height: height)
        }
        var options = ckey_scan_options()
        ckey_scan_options_init(&options)
        options.max_candidates = UInt32(fast ? 2 : maxCandidates)
        if fast { options.flags |= CKEY_SCAN_OPTION_FAST }
        // The ML locator's proposals only after the border search found nothing
        // that decodes, so frames the classical reader reads pay nothing for it.
        // Without a model (see `setMachineLearning(enabled:)`) this is a no-op.
        options.flags |= CKEY_SCAN_OPTION_ML_AFTER
        if let region = region?.standardized, region.width >= 1, region.height >= 1,
           region.minX.isFinite, region.minY.isFinite, region.width.isFinite, region.height.isFinite {
            options.roi_x = Double(region.minX)
            options.roi_y = Double(region.minY)
            options.roi_width = Double(region.width)
            options.roi_height = Double(region.height)
        }
        var found = ckey_scan_result()
        let capacity = payload.count
        let status = payload.withUnsafeMutableBufferPointer { output -> ckey_status in
            if let accumulator {
                return ckey_scan_frame_accumulate_gray8(gray, width, height, stride, &options, accumulator,
                                                        output.baseAddress, capacity, &found)
            }
            return ckey_scan_frame_gray8(gray, width, height, stride, &options, output.baseAddress, capacity, &found)
        }
        var result: CKeyResult?
        let length = Int(found.payload_length)
        if status == CKEY_OK, length <= capacity, let profile = CKeyProfile(rawValue: Int(found.profile)) {
            let bytes = Data(payload[0..<length])
            if profile == .scalable {
                result = CKeyResult(bytes: bytes, profile: .scalable, version: Int(found.version),
                                    layout: CKeyLayout(rawValue: Int(found.layout)) ?? .standard)
            } else {
                result = CKeyResult(bytes: bytes, profile: profile)
            }
        }
        let located = found.located != 0
        let q = found.quad
        let quad = located ? [CGPoint(x: q.0, y: q.1), CGPoint(x: q.2, y: q.3),
                              CGPoint(x: q.4, y: q.5), CGPoint(x: q.6, y: q.7)] : []
        var outcome = CKeyScanOutcome(result: result, located: located, quad: quad, score: found.score,
                               symbolCut: found.flags & CKEY_SCAN_FLAG_SYMBOL_CUT != 0,
                               budgetExhausted: found.flags & CKEY_SCAN_FLAG_BUDGET != 0,
                               correctedSymbols: Int(found.corrected_symbols),
                               erasuresUsed: Int(found.erasures_used),
                               candidates: Int(found.candidates),
                               frameSize: CGSize(width: width, height: height))
        outcome.accumulated = found.flags & CKEY_SCAN_FLAG_ACCUMULATED != 0
        return outcome
    }
}

/// Access to a pixel buffer's 8-bit luma.
enum CKeyLuma {
    /// Runs `body(gray, width, height, stride)` on the buffer's luma: the luma
    /// plane of the planar 4:2:0 formats and one-component gray in place, and
    /// a converted copy for 32-bit BGRA/RGBA/ARGB. Returns `nil` for other formats.
    static func withGray<T>(_ pixelBuffer: CVPixelBuffer,
                            _ body: (UnsafePointer<UInt8>, Int, Int, Int) -> T) -> T? {
        CVPixelBufferLockBaseAddress(pixelBuffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, .readOnly) }
        let format = CVPixelBufferGetPixelFormatType(pixelBuffer)
        switch format {
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
             kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
             kCVPixelFormatType_420YpCbCr8Planar,
             kCVPixelFormatType_420YpCbCr8PlanarFullRange:
            guard CVPixelBufferGetPlaneCount(pixelBuffer) >= 1,
                  let base = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0) else { return nil }
            return body(UnsafePointer(base.assumingMemoryBound(to: UInt8.self)),
                        CVPixelBufferGetWidthOfPlane(pixelBuffer, 0),
                        CVPixelBufferGetHeightOfPlane(pixelBuffer, 0),
                        CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0))
        case kCVPixelFormatType_OneComponent8:
            guard let base = CVPixelBufferGetBaseAddress(pixelBuffer) else { return nil }
            return body(UnsafePointer(base.assumingMemoryBound(to: UInt8.self)),
                        CVPixelBufferGetWidth(pixelBuffer),
                        CVPixelBufferGetHeight(pixelBuffer),
                        CVPixelBufferGetBytesPerRow(pixelBuffer))
        case kCVPixelFormatType_32BGRA, kCVPixelFormatType_32RGBA, kCVPixelFormatType_32ARGB:
            guard let base = CVPixelBufferGetBaseAddress(pixelBuffer) else { return nil }
            let width = CVPixelBufferGetWidth(pixelBuffer)
            let height = CVPixelBufferGetHeight(pixelBuffer)
            let rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer)
            guard width > 0, height > 0 else { return nil }
            // Channel offsets of R, G, B within each 4-byte pixel.
            let offsets: (r: Int, g: Int, b: Int)
            if format == kCVPixelFormatType_32BGRA {
                offsets = (r: 2, g: 1, b: 0)
            } else if format == kCVPixelFormatType_32RGBA {
                offsets = (r: 0, g: 1, b: 2)
            } else {
                offsets = (r: 1, g: 2, b: 3)
            }
            let source = UnsafePointer(base.assumingMemoryBound(to: UInt8.self))
            var gray = [UInt8](repeating: 0, count: width * height)
            for y in 0..<height {
                let row = source + y * rowBytes
                for x in 0..<width {
                    let p = row + x * 4
                    // Rec. 709 luma, integer weights summing to 256.
                    let luma = (54 * Int(p[offsets.r]) + 183 * Int(p[offsets.g]) + 19 * Int(p[offsets.b])) >> 8
                    gray[y * width + x] = UInt8(min(255, luma))
                }
            }
            return gray.withUnsafeBufferPointer { buffer in
                buffer.baseAddress.map { body($0, width, height, width) }
            }
        default:
            return nil
        }
    }
}

/// A CGImage drawn over white into tightly packed 8-bit gray.
struct GrayFrame {
    let width: Int
    let height: Int
    let bytes: [UInt8]

    init?(_ image: CGImage) {
        let width = image.width, height = image.height
        guard width > 0, height > 0 else { return nil }
        var bytes = [UInt8](repeating: 255, count: width * height)
        let drawn = bytes.withUnsafeMutableBytes { raw -> Bool in
            guard let context = CGContext(data: raw.baseAddress, width: width, height: height,
                                          bitsPerComponent: 8, bytesPerRow: width,
                                          space: CGColorSpaceCreateDeviceGray(),
                                          bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return false }
            context.setFillColor(gray: 1, alpha: 1)
            context.fill(CGRect(x: 0, y: 0, width: width, height: height))
            context.interpolationQuality = .none
            context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
            return true
        }
        guard drawn else { return nil }
        self.width = width
        self.height = height
        self.bytes = bytes
    }
}
