import CKey
import CoreGraphics
import CoreVideo
import XCTest

final class CKeyRoundTripTests: XCTestCase {
    func testEncodeRenderDecodeText() throws {
        let symbol = try CKeySymbol.encode("Hello, CKey")
        XCTAssertGreaterThanOrEqual(symbol.version, 1)
        XCTAssertEqual(symbol.level, .m)
        XCTAssertEqual(symbol.layout, .standard)
        XCTAssertEqual(symbol.payloadLength, 11)

        let image = try symbol.cgImage(size: 512)
        XCTAssertEqual(image.width, 512)
        XCTAssertEqual(image.height, 512)
        let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: image))
        XCTAssertEqual(result.bytes, Data("Hello, CKey".utf8))
        XCTAssertEqual(result.text, "Hello, CKey")
        XCTAssertEqual(result.profile, .scalable)
        XCTAssertEqual(result.version, symbol.version)
        XCTAssertEqual(result.layout, .standard)
        XCTAssertEqual(result.telemetryProfileCode, 0)
    }

    func testPixelPerfectRendersWholeCellsAndDecodes() throws {
        for layout in [CKeyLayout.standard, .print] {
            let text = String(repeating: "pixel-perfect ", count: 20)
            let symbol = try CKeySymbol.encode(text, layout: layout)
            let lattice = try symbol.lattice()
            XCTAssertEqual(lattice.cells.count, lattice.side * lattice.side)
            XCTAssertTrue(lattice.cells.allSatisfy { $0 <= 1 })
            XCTAssertEqual(lattice.pxPerCell(fitting: lattice.side * 3 + 2), 3)
            let image = try symbol.pixelPerfectImage(pxPerCell: 2)
            XCTAssertEqual(image.width, lattice.side * 2)
            let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: image))
            XCTAssertEqual(result.text, text)
            let pixels = try symbol.pixelPerfectPixels(pxPerCell: 2)
            var colours = Set<UInt32>()
            pixels.rgba.withUnsafeBytes { raw in
                let b = raw.bindMemory(to: UInt8.self)
                for i in stride(from: 0, to: b.count, by: 4) { colours.insert(UInt32(b[i]) << 16 | UInt32(b[i + 1]) << 8 | UInt32(b[i + 2])) }
            }
            XCTAssertEqual(colours.count, 2, "only background and ink: nothing blended")
            let png = try symbol.pixelPerfectPNG(pxPerCell: 3, dpi: 300)
            XCTAssertEqual(Array(png.prefix(4)), [0x89, 0x50, 0x4E, 0x47])
            let svg = try symbol.pixelPerfectSVG(pxPerCell: 3)
            XCTAssertTrue(svg.contains("width=\"\(lattice.side * 3)\""))
            XCTAssertTrue(svg.contains("shape-rendering=\"crispEdges\""))
        }
        let symbol = try CKeySymbol.encode("x")
        XCTAssertThrowsError(try symbol.lattice(cellsPerModule: 7))
        XCTAssertThrowsError(try symbol.pixelPerfectImage(pxPerCell: 0))
    }

    func testTransparentRenderingDecodesOverWhite() throws {
        let symbol = try CKeySymbol.encode("https://openckey.org")
        let image = try symbol.cgImage(size: 400, transparent: true)
        XCTAssertEqual(image.alphaInfo, .last)
        let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: image))
        XCTAssertEqual(result.text, "https://openckey.org")
    }

    func testUltraAndExtremeLevelsAndGroupedLayouts() throws {
        // The Swift values are the core's (ckey_ecc_level, ckey_scalable_layout).
        XCTAssertEqual(CKeyErrorCorrection.l.rawValue, 0)
        XCTAssertEqual(CKeyErrorCorrection.m.rawValue, 1)
        XCTAssertEqual(CKeyErrorCorrection.q.rawValue, 2)
        XCTAssertEqual(CKeyErrorCorrection.h.rawValue, 3)
        XCTAssertEqual(CKeyErrorCorrection.u.rawValue, 4)   // CKEY_ECC_U
        XCTAssertEqual(CKeyErrorCorrection.x.rawValue, 5)   // CKEY_ECC_X
        XCTAssertEqual(CKeyErrorCorrection.allCases.count, 6)
        XCTAssertEqual(CKeyLayout.standardGrouped.rawValue, 2)
        XCTAssertEqual(CKeyLayout.printGrouped.rawValue, 3)

        // More parity, less payload: U carries less than H, X less than U.
        let h = CKeySymbol.capacity(version: 20, level: .h)
        let u = CKeySymbol.capacity(version: 20, level: .u)
        let x = CKeySymbol.capacity(version: 20, level: .x)
        XCTAssertGreaterThan(u, 0)
        XCTAssertLessThan(u, h)
        XCTAssertLessThan(x, u)
        // The defaults are unchanged.
        let plain = try CKeySymbol.encode("CKey")
        XCTAssertEqual(plain.level, .m)
        XCTAssertEqual(plain.layout, .standard)

        let ultra = try CKeySymbol.encode("CKey Ultra", level: .u)
        XCTAssertEqual(ultra.level, .u)
        let grouped = try CKeySymbol.encode("CKey grouped", level: .q, layout: .standardGrouped)
        XCTAssertEqual(grouped.layout, .standardGrouped)
        let image = try grouped.cgImage(size: 1024)
        let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: image))
        XCTAssertEqual(result.text, "CKey grouped")
        XCTAssertEqual(result.layout, .standardGrouped)
    }

    func testEveryPolicyAndPrintLayoutRoundTrip() throws {
        for layout in CKeyLayout.allCases {
            for policy in CKeyRasterPolicy.allCases {
                let symbol = try CKeySymbol.encode("CKey \(layout) \(policy)", level: .q, layout: layout)
                let image = try symbol.cgImage(size: 600, policy: policy)
                let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: image), "\(layout) \(policy)")
                XCTAssertEqual(result.text, "CKey \(layout) \(policy)")
                XCTAssertEqual(result.layout, layout)
            }
        }
    }

    func testBinaryPayloadRoundTripThroughPixelBuffer() throws {
        let payload = Data((0..<37).map { UInt8(($0 * 37 + 11) & 0xff) } + [0, 1, 2])
        let symbol = try CKeySymbol.encode(payload, version: 6, level: .h)
        XCTAssertEqual(symbol.version, 6)
        let pixels = try symbol.pixels(size: 640)

        var buffer: CVPixelBuffer?
        XCTAssertEqual(CVPixelBufferCreate(nil, pixels.width, pixels.height, kCVPixelFormatType_32BGRA, nil, &buffer),
                       kCVReturnSuccess)
        let pixelBuffer = try XCTUnwrap(buffer)
        CVPixelBufferLockBaseAddress(pixelBuffer, [])
        let base = try XCTUnwrap(CVPixelBufferGetBaseAddress(pixelBuffer)).assumingMemoryBound(to: UInt8.self)
        let rowBytes = CVPixelBufferGetBytesPerRow(pixelBuffer)
        pixels.rgba.withUnsafeBytes { raw in
            let rgba = raw.bindMemory(to: UInt8.self)
            for y in 0..<pixels.height {
                for x in 0..<pixels.width {
                    let s = (y * pixels.width + x) * 4
                    let d = base + y * rowBytes + x * 4
                    d[0] = rgba[s + 2]; d[1] = rgba[s + 1]; d[2] = rgba[s]; d[3] = rgba[s + 3]
                }
            }
        }
        CVPixelBufferUnlockBaseAddress(pixelBuffer, [])

        let result = try XCTUnwrap(CKeyDecoder.decode(pixelBuffer: pixelBuffer))
        XCTAssertEqual(result.bytes, payload)
        XCTAssertNil(result.text, "binary payloads have no text")
        XCTAssertEqual(result.version, 6)
    }

    func testBlankImageDoesNotDecode() throws {
        let symbol = try CKeySymbol.encode("x")
        // A plain white image has no border candidates.
        let space = CGColorSpaceCreateDeviceGray()
        let context = try XCTUnwrap(CGContext(data: nil, width: 300, height: 300, bitsPerComponent: 8, bytesPerRow: 300,
                                              space: space, bitmapInfo: CGImageAlphaInfo.none.rawValue))
        context.setFillColor(gray: 1, alpha: 1)
        context.fill(CGRect(x: 0, y: 0, width: 300, height: 300))
        XCTAssertNil(CKeyDecoder.decode(cgImage: try XCTUnwrap(context.makeImage())))
        XCTAssertNotNil(CKeyDecoder.decode(cgImage: try symbol.cgImage(size: 300)))
    }
}

final class CKeyExportTests: XCTestCase {
    func testPNGHeader() throws {
        let png = try CKeySymbol.encode("PNG").pngData(size: 256, transparent: true, dpi: 300)
        XCTAssertGreaterThan(png.count, 64)
        XCTAssertEqual(Array(png.prefix(8)), [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
        XCTAssertEqual(Array(png[12..<16]), Array("IHDR".utf8))
    }

    func testPDFHeaderAndTrailer() throws {
        let pdf = try CKeySymbol.encode("PDF").pdfData(widthPoints: 144)
        XCTAssertEqual(Array(pdf.prefix(5)), Array("%PDF-".utf8))
        let tail = String(decoding: pdf.suffix(32), as: UTF8.self)
        XCTAssertTrue(tail.contains("%%EOF"), tail)
    }

    func testSVG() throws {
        let svg = try CKeySymbol.encode("SVG").svg(scale: 8, style: .concept)
        XCTAssertTrue(svg.contains("<svg"))
        XCTAssertTrue(svg.hasSuffix(">") || svg.hasSuffix(">\n"))
        XCTAssertFalse(svg.contains("\u{0}"))
    }

    func testTransparentAlpha() throws {
        let symbol = try CKeySymbol.encode("alpha")
        let transparent = try symbol.pixels(size: 128, transparent: true)
        XCTAssertEqual(transparent.rgba.count, 128 * 128 * 4)
        XCTAssertTrue(transparent.hasAlpha)
        XCTAssertEqual(transparent.rgba[3], 0, "the quiet-margin corner is unpainted")
        XCTAssertTrue(stride(from: 3, to: transparent.rgba.count, by: 4).contains { transparent.rgba[$0] == 255 },
                      "ink is opaque")

        let opaque = try symbol.pixels(size: 128, transparent: false)
        XCTAssertFalse(opaque.hasAlpha)
        XCTAssertTrue(stride(from: 3, to: opaque.rgba.count, by: 4).allSatisfy { opaque.rgba[$0] == 255 })

        let image = try symbol.cgImage(size: 128, transparent: true)
        let data = try XCTUnwrap(image.dataProvider?.data) as Data
        XCTAssertEqual(data[3], 0)
    }

    func testStylesAndCapacity() throws {
        XCTAssertEqual(CKeyStyle(), CKeyStyle.plain)
        XCTAssertEqual(CKeyStyle.plain.ink, .black)
        XCTAssertEqual(CKeyStyle.plain.policy, .strict)
        XCTAssertNotEqual(CKeyStyle.concept, CKeyStyle.plain)

        let symbol = try CKeySymbol.encode("style")
        XCTAssertNoThrow(try symbol.check(style: .plain))
        XCTAssertNoThrow(try symbol.cgImage(size: 256, style: .concept))

        var unreadable = CKeyStyle.plain
        unreadable.ink = CKeyColor(red: 250, green: 250, blue: 250)
        unreadable.inkShade = unreadable.ink
        XCTAssertThrowsError(try symbol.check(style: unreadable))
        XCTAssertThrowsError(try symbol.cgImage(size: 256, style: unreadable), "strict policy refuses")
        unreadable.policy = .override
        XCTAssertNoThrow(try symbol.cgImage(size: 256, style: unreadable), "override draws as given")

        XCTAssertGreaterThan(CKeySymbol.capacity(version: 1), 0)
        XCTAssertEqual(CKeySymbol.capacity(version: 0), 0)
        XCTAssertThrowsError(try CKeySymbol.encode("x", version: 32))
        let tooLong = String(repeating: "A", count: CKeySymbol.capacity(version: 1) + 1)
        XCTAssertThrowsError(try CKeySymbol.encode(tooLong, version: 1)) { error in
            XCTAssertTrue(error is CKeyError)
        }
    }

    func testCertification() throws {
        let symbol = try CKeySymbol.encode("certify")
        let plain = try symbol.certify()
        XCTAssertTrue(plain.accepted, plain.message)
        XCTAssertTrue(plain.certified, plain.message)

        let unknownBackground = try symbol.certify(transparent: true)
        XCTAssertFalse(unknownBackground.certified, "a transparent symbol over an unknown background is never certified")

        let onWhite = try symbol.certify(transparent: true, backdrops: [.white])
        XCTAssertTrue(onWhite.certified, onWhite.message)
    }
}

final class CKeyTelemetryTests: XCTestCase {
    override func tearDown() {
        CKey.telemetry = .on
        super.tearDown()
    }

    func testDefaultAndValidation() throws {
        XCTAssertEqual(CKeyTelemetryMode.on.rawValue, 1)
        CKey.telemetry = .local
        for invalid in [-1, 3, 42, Int.max] {
            XCTAssertThrowsError(try CKey.setTelemetry(rawValue: invalid)) { error in
                XCTAssertEqual((error as? CKeyError)?.code, .invalidConfiguration)
            }
            XCTAssertEqual(CKey.telemetry, .local, "an invalid value leaves the mode unchanged")
        }
        try CKey.setTelemetry(rawValue: 0)
        XCTAssertEqual(CKey.telemetry, .off)
        try CKey.setTelemetry(rawValue: 1)
        XCTAssertEqual(CKey.telemetry, .on)
        CKey.telemetry = 2
        XCTAssertEqual(CKey.telemetry, .local)
        CKey.telemetry = 0
        XCTAssertEqual(CKey.telemetry, .off)
        XCTAssertNil(CKeyTelemetryMode(rawValue: 5))
    }

    func testLocalModeCountsSessionsAndOffPurges() throws {
        CKey.telemetry = .off           // purge
        CKey.telemetry = .local         // count from zero, never send
        let session = CKeyTelemetrySession.shared
        let symbol = try CKeySymbol.encode("telemetry")
        let result = try XCTUnwrap(CKeyDecoder.decode(cgImage: try symbol.cgImage(size: 400)))

        session.scanSessionDidBegin()
        session.scanFrameProcessed(milliseconds: 12)
        session.scanSessionDidDecode(result)
        session.scanSessionDidEnd()
        session.scanSessionDidBegin()
        session.scanSessionDidEnd()     // no decode: fails

        let counters = CKey.telemetryCounters
        XCTAssertEqual(counters.sessionsStarted, 2)
        XCTAssertEqual(counters.sessionsSucceeded, 1)
        XCTAssertEqual(counters.sessionsFailed, 1)
        XCTAssertEqual(counters.frames, 1)
        XCTAssertEqual(counters.decoded["p0"], 1)
        XCTAssertEqual(counters.timeToDecodeMilliseconds.count, 1)
        XCTAssertEqual(counters.frameMilliseconds.buckets.count, 10)
        XCTAssertEqual(counters.frameMilliseconds.buckets.reduce(0, +), counters.frameMilliseconds.count)

        CKey.telemetry = .off
        let purged = CKey.telemetryCounters
        XCTAssertEqual(purged.sessionsStarted, 0)
        XCTAssertEqual(purged.frames, 0)
        session.scanSessionDidBegin()   // mode 0: a no-op
        XCTAssertEqual(CKey.telemetryCounters.sessionsStarted, 0)
    }

    func testVersion() {
        XCTAssertTrue(CKey.version.hasPrefix("0."), CKey.version)
        XCTAssertEqual(CKeyCoreInfo.softwareVersion, CKey.version)
    }
}
