import CKeyScanKit
import CoreGraphics
import CoreVideo
import XCTest

/// The scan-only product: no renderer and no telemetry are linked here.
final class CKeyPayloadTests: XCTestCase {
    func testTextDetection() {
        XCTAssertEqual(CKeyPayload.text(from: Data("hello\r\nworld\t!".utf8)), "hello\r\nworld\t!")
        XCTAssertNil(CKeyPayload.text(from: Data([0xff, 0xfe])), "invalid UTF-8")
        XCTAssertNil(CKeyPayload.text(from: Data([0x41, 0x00, 0x42])), "NUL is a control character")
        XCTAssertNil(CKeyPayload.text(from: Data([0x41, 0x1b])), "ESC is a control character")
        XCTAssertNil(CKeyPayload.text(from: Data()))
        XCTAssertEqual(CKeyPayload.hex(Data([0x00, 0xab, 0x10])), "00ab10")
    }

    func testAllowlistedForms() {
        XCTAssertEqual(CKeyPayload.classify("  https://citeory.com/tour \n"), .web(URL(string: "https://citeory.com/tour")!))
        XCTAssertEqual(CKeyPayload.classify("HTTP://example.com"), .web(URL(string: "HTTP://example.com")!))
        XCTAssertEqual(CKeyPayload.classify("mailto:hello@citeory.com?subject=Hi"),
                       .email(URL(string: "mailto:hello@citeory.com?subject=Hi")!, address: "hello@citeory.com"))
        XCTAssertEqual(CKeyPayload.classify("tel:+1 (555) 123-4567"),
                       .call(URL(string: "tel:+15551234567")!, number: "+15551234567"))
        if case .message(_, let number)? = CKeyPayload.classify("sms:+15551234567?body=Hi") {
            XCTAssertEqual(number, "+15551234567")
        } else {
            XCTFail("sms")
        }
        if case .geo(let lat, let lon, let label, let url)? = CKeyPayload.classify("geo:37.33490,-122.00900?q=Apple%20Park") {
            XCTAssertEqual(lat, 37.3349, accuracy: 1e-9)
            XCTAssertEqual(lon, -122.009, accuracy: 1e-9)
            XCTAssertEqual(label, "Apple Park")
            XCTAssertEqual(url.host, "maps.apple.com")
        } else {
            XCTFail("geo")
        }
        if case .address(let address, let url)? = CKeyPayload.classify("address:1 Infinite Loop\n\nCupertino, CA 95014") {
            XCTAssertEqual(address, "1 Infinite Loop, Cupertino, CA 95014")
            XCTAssertTrue(url.absoluteString.contains("address="))
        } else {
            XCTFail("address")
        }
        let card = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Ada Lovelace\r\nEND:VCARD"
        XCTAssertEqual(CKeyPayload.classify(card), .contact(vCard: card, name: "Ada Lovelace"))
    }

    func testRejectedForms() {
        for text in ["javascript:alert(1)", "data:text/html,hi", "file:///etc/passwd", "intent://x",
                     "myapp://open", "mailto:nobody", "tel:12", "sms:", "geo:91,0", "geo:0,181", "geo:abc",
                     "address:   \n  ", "https://", "plain words", "BEGIN:VCARD\nFN:x"] {
            XCTAssertNil(CKeyPayload.classify(text), text)
        }
    }
}

final class CKeyDecoderTests: XCTestCase {
    func testBlankFramesDoNotDecode() throws {
        let white = [UInt8](repeating: 255, count: 320 * 240)
        white.withUnsafeBufferPointer { buffer in
            XCTAssertNil(CKeyDecoder.decode(gray: buffer.baseAddress!, width: 320, height: 240, stride: 320))
        }
        var pixelBuffer: CVPixelBuffer?
        XCTAssertEqual(CVPixelBufferCreate(nil, 64, 64, kCVPixelFormatType_OneComponent8, nil, &pixelBuffer),
                       kCVReturnSuccess)
        XCTAssertNil(CKeyDecoder.decode(pixelBuffer: try XCTUnwrap(pixelBuffer)))
    }

    func testInvalidGeometryIsRejected() {
        let gray = [UInt8](repeating: 0, count: 16)
        gray.withUnsafeBufferPointer { buffer in
            XCTAssertNil(CKeyDecoder.decode(gray: buffer.baseAddress!, width: 4, height: 4, stride: 2))
        }
    }

    func testResultMetadata() {
        let result = CKeyResult(bytes: Data("hi".utf8), profile: .scalable, version: 3, layout: .print)
        XCTAssertEqual(result.text, "hi")
        XCTAssertEqual(result.telemetryProfileCode, 1)
        XCTAssertEqual(result.profileDescription, "Profile 0.1 · print · v3")
        XCTAssertEqual(CKeyResult(bytes: Data(count: 16), profile: .locator).telemetryProfileCode, 2)
        XCTAssertFalse(CKeyCoreInfo.softwareVersion.isEmpty)
        XCTAssertGreaterThanOrEqual(CKeyCoreInfo.abiVersion, 4)
    }
}

final class CKeyPayloadEdgeCaseTests: XCTestCase {
    func testPhoneParametersAreNeverDialled() {
        guard case .call(let url, let number)? = CKeyPayload.classify("tel:+15550100;ext=7") else { return XCTFail("tel") }
        XCTAssertEqual(number, "+15550100")
        XCTAssertEqual(url.absoluteString, "tel:+15550100")
        guard case .message(_, let sms)? = CKeyPayload.classify("sms:+15550100?body=Hi") else { return XCTFail("sms") }
        XCTAssertEqual(sms, "+15550100")
    }

    func testFormatCharactersAreText() {
        // Family emoji (ZWJ sequence), a soft hyphen and a BOM are text, not binary.
        let text = "👨‍👩‍👧 co\u{00AD}operate \u{FEFF}hello"
        XCTAssertEqual(CKeyPayload.text(from: Data(text.utf8)), text)
        XCTAssertNil(CKeyPayload.text(from: Data([0x41, 0x07, 0x42])))  // BEL is a control character
        XCTAssertNil(CKeyPayload.text(from: Data("a\u{0085}b".utf8)))   // C1 control
    }

    func testVCardMustBeComplete() {
        XCTAssertNotNil(CKeyPayload.classify("BEGIN:VCARD\nVERSION:4.0\nFN:Ada\nEND:VCARD"))
        XCTAssertNil(CKeyPayload.classify("BEGIN:VCARD\nFN:Ada\nEND:VCARD trailing words"))
        XCTAssertNil(CKeyPayload.classify("BEGIN:VCARDS and more END:VCARD"))
    }
}
