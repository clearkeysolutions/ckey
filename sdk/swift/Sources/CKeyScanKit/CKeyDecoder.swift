import CKeyCodec
import CoreGraphics
import CoreVideo
import Foundation

/// Headless decoding of still images and camera frames.
///
/// The pipeline is the iOS reader's (ios/CKeyReader/CameraScanner.swift),
/// entirely in the C++ core: find up to six border candidates in the whole
/// frame (`ckey_find_borders_gray8`, both polarities), fit each one to
/// Profile 0's field first (`ckey_refine_quad_gray8` with
/// `CKEY_FIELD_SCALABLE`) and decode it, then try Profiles 1, 2 and 3, each
/// refined against its own field. A payload is returned only after the
/// core's Reed-Solomon correction and CRC-32C checks accept it.
///
/// All functions are synchronous, reentrant and safe to call from any
/// thread; call them off the main thread for camera-sized frames.
public enum CKeyDecoder {
    /// Maximum border candidates tried per frame (as in the iOS reader).
    public static let maxCandidates = 6

    /// Decodes an 8-bit grayscale frame (`stride` bytes per row). Returns
    /// `nil` when no symbol decodes.
    public static func decode(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int) -> CKeyResult? {
        guard width > 0, height > 0, stride >= width else { return nil }
        var borders = [Double](repeating: 0, count: maxCandidates * 8)
        let count = borders.withUnsafeMutableBufferPointer { output in
            ckey_find_borders_gray8(gray, width, height, stride, output.baseAddress, maxCandidates)
        }
        guard count > 0 else { return nil }
        // Profile 0 payloads reach 4,619 bytes (v31, level M): 64 KB output.
        var output = [UInt8](repeating: 0, count: 65535)
        for index in 0..<min(count, maxCandidates) {
            let border = Array(borders[(index * 8)..<(index * 8 + 8)])
            if let quad = refine(border, gray, width, height, stride, CKEY_FIELD_SCALABLE),
               let result = decodeScalable(gray, width, height, stride, quad, &output) {
                return result
            }
            if let result = decodeLegacy(gray, width, height, stride, border, &output) {
                return result
            }
        }
        return nil
    }

    /// Decodes an image. It is drawn over white into an 8-bit gray buffer,
    /// so a transparent rendering (dark ink, alpha 0 elsewhere) reads as
    /// dark-on-white. Returns `nil` when no symbol decodes.
    public static func decode(cgImage image: CGImage) -> CKeyResult? {
        guard let frame = GrayFrame(image) else { return nil }
        return frame.bytes.withUnsafeBufferPointer { buffer in
            guard let base = buffer.baseAddress else { return nil }
            return decode(gray: base, width: frame.width, height: frame.height, stride: frame.width)
        }
    }

    /// Decodes a camera or video frame. Supported formats: the bi-planar
    /// 4:2:0 YpCbCr formats (luma plane, as AVFoundation delivers by
    /// default), 8-bit one-component gray, and 32-bit BGRA/RGBA/ARGB (converted
    /// to luma). Returns `nil` when no symbol decodes or the format is unsupported.
    public static func decode(pixelBuffer: CVPixelBuffer) -> CKeyResult? {
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
            return decode(gray: base.assumingMemoryBound(to: UInt8.self),
                          width: CVPixelBufferGetWidthOfPlane(pixelBuffer, 0),
                          height: CVPixelBufferGetHeightOfPlane(pixelBuffer, 0),
                          stride: CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0))
        case kCVPixelFormatType_OneComponent8:
            guard let base = CVPixelBufferGetBaseAddress(pixelBuffer) else { return nil }
            return decode(gray: base.assumingMemoryBound(to: UInt8.self),
                          width: CVPixelBufferGetWidth(pixelBuffer),
                          height: CVPixelBufferGetHeight(pixelBuffer),
                          stride: CVPixelBufferGetBytesPerRow(pixelBuffer))
        case kCVPixelFormatType_32BGRA, kCVPixelFormatType_32RGBA, kCVPixelFormatType_32ARGB:
            guard let base = CVPixelBufferGetBaseAddress(pixelBuffer) else { return nil }
            let width = CVPixelBufferGetWidth(pixelBuffer)
            let height = CVPixelBufferGetHeight(pixelBuffer)
            let rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer)
            // Channel offsets of R, G, B within each 4-byte pixel.
            let offsets: (r: Int, g: Int, b: Int)
            if format == kCVPixelFormatType_32BGRA {
                offsets = (r: 2, g: 1, b: 0)
            } else if format == kCVPixelFormatType_32RGBA {
                offsets = (r: 0, g: 1, b: 2)
            } else {
                offsets = (r: 1, g: 2, b: 3)
            }
            let source = base.assumingMemoryBound(to: UInt8.self)
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
                buffer.baseAddress.flatMap { decode(gray: $0, width: width, height: height, stride: width) }
            }
        default:
            return nil
        }
    }

    // MARK: - Pipeline steps

    private static func refine(_ border: [Double], _ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int,
                               _ stride: Int, _ field: ckey_field) -> [Double]? {
        var refined = [Double](repeating: 0, count: 8)
        let score = border.withUnsafeBufferPointer { input in
            refined.withUnsafeMutableBufferPointer { output in
                ckey_refine_quad_gray8(gray, width, height, stride, input.baseAddress, field, output.baseAddress)
            }
        }
        return score >= 0 ? refined : nil
    }

    private static func decodeScalable(_ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int, _ stride: Int,
                                       _ quad: [Double], _ output: inout [UInt8]) -> CKeyResult? {
        var length = 0
        var version: UInt32 = 0
        var layout: UInt32 = 0
        var diagnostics = ckey_diagnostics()
        let status = output.withUnsafeMutableBufferPointer { decoded in
            quad.withUnsafeBufferPointer { q in
                ckey_decode_scalable_gray8(gray, width, height, stride, q.baseAddress,
                                           decoded.baseAddress, decoded.count, &length, &version, &layout, &diagnostics)
            }
        }
        guard status == CKEY_OK, length <= output.count else { return nil }
        return CKeyResult(bytes: Data(output[0..<length]), profile: .scalable, version: Int(version),
                          layout: CKeyLayout(rawValue: Int(layout)) ?? .standard)
    }

    private static func decodeLegacy(_ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int, _ stride: Int,
                                     _ border: [Double], _ output: inout [UInt8]) -> CKeyResult? {
        var diagnostics = ckey_diagnostics()
        // Profile 1: the compact locator (fast path, fixed 16 bytes).
        if let quad = refine(border, gray, width, height, stride, CKEY_FIELD_LOCATOR) {
            let capacity = Int(CKEY_LOCATOR_PAYLOAD_BYTES)
            let status = output.withUnsafeMutableBufferPointer { decoded in
                quad.withUnsafeBufferPointer { q in
                    ckey_decode_gray8(gray, width, height, stride, q.baseAddress, decoded.baseAddress, capacity, &diagnostics)
                }
            }
            if status == CKEY_OK { return CKeyResult(bytes: Data(output[0..<capacity]), profile: .locator) }
        }
        // Profiles 2 and 3: dense fields, each refined against its own keys.
        var length = 0
        if let quad = refine(border, gray, width, height, stride, CKEY_FIELD_TEXT) {
            let status = output.withUnsafeMutableBufferPointer { decoded in
                quad.withUnsafeBufferPointer { q in
                    ckey_decode_text_gray8(gray, width, height, stride, q.baseAddress,
                                           decoded.baseAddress, decoded.count, &length, &diagnostics)
                }
            }
            if status == CKEY_OK, length <= output.count {
                return CKeyResult(bytes: Data(output[0..<length]), profile: .text)
            }
        }
        length = 0
        if let quad = refine(border, gray, width, height, stride, CKEY_FIELD_EXTENDED) {
            let status = output.withUnsafeMutableBufferPointer { decoded in
                quad.withUnsafeBufferPointer { q in
                    ckey_decode_extended_gray8(gray, width, height, stride, q.baseAddress,
                                               decoded.baseAddress, decoded.count, &length, &diagnostics)
                }
            }
            if status == CKEY_OK, length <= output.count {
                return CKeyResult(bytes: Data(output[0..<length]), profile: .extended)
            }
        }
        return nil
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
