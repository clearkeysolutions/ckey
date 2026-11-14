import CKeyScanKit
import XCTest

/// Safe Actions against the conformance vectors (tests/vectors/safe-actions-v1.json).
final class CKeySafeActionsTests: XCTestCase {
    private struct Vector: Decodable {
        struct Fields: Decodable { let scope: Int; let issuer: String; let app: String; let action: String; let key_id: String; let request_id: String }
        let name: String
        let payload: String
        let public_key: String
        let now: Double
        let max_lifetime: UInt32
        let expect: String
        let fields: Fields?
    }

    private func vectors(_ file: String = "safe-actions-v1.json") throws -> [Vector] {
        // Tests/CKeyScanKitTests -> repository root.
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let data = try Data(contentsOf: root.appendingPathComponent("tests/vectors/" + file))
        struct File: Decodable { let vectors: [Vector] }
        return try JSONDecoder().decode(File.self, from: data).vectors
    }

    func testSameDeviceVectorsRunAndBadOnesAreRefused() async throws {
        for v in try vectors() where v.fields?.scope == 0 || v.expect == "integrity" {
            let ran = expectation(description: v.name)
            ran.isInverted = v.expect != "ok"
            let issuer = v.fields?.issuer ?? "citeory.com"
            let actions = CKeySafeActions(appID: v.fields?.app ?? "com.viadere", maxLifetime: v.max_lifetime,
                                          now: { Date(timeIntervalSince1970: v.now) })
                .trust(issuer: issuer, keyID: v.fields?.key_id ?? "2026-11", publicKeyHex: v.public_key)
                .register(v.fields?.action ?? "open.library", issuers: [issuer]) { _ in ran.fulfill() }
            let outcome = await actions.handle(v.payload)
            switch outcome {
            case .performed: XCTAssertEqual(v.expect, "ok", v.name)
            case .rejected: XCTAssertNotEqual(v.expect, "ok", v.name)
            default: XCTFail("\(v.name): unexpected \(outcome)")
            }
            await fulfillment(of: [ran], timeout: 0.5)
        }
    }

    /// ES256 keys (tests/vectors/safe-actions-es256-v1.json): pinned by their
    /// length (66 or 130 hex digits); a high-S or wrong-key signature is refused.
    func testES256VectorsRunAndBadOnesAreRefused() async throws {
        for v in try vectors("safe-actions-es256-v1.json") where v.expect != "invalid" && (v.fields?.scope ?? 0) == 0 {
            let ran = expectation(description: v.name)
            ran.isInverted = v.expect != "ok"
            let issuer = v.fields?.issuer ?? "citeory.com"
            let actions = CKeySafeActions(appID: v.fields?.app ?? "com.viadere", maxLifetime: v.max_lifetime,
                                          now: { Date(timeIntervalSince1970: v.now) })
                .trust(issuer: issuer, keyID: v.fields?.key_id ?? "vault-p256-1", publicKeyHex: v.public_key)
                .register(v.fields?.action ?? "open.library", issuers: [issuer]) { _ in ran.fulfill() }
            let outcome = await actions.handle(v.payload)
            switch outcome {
            case .performed: XCTAssertEqual(v.expect, "ok", v.name)
            case .rejected: XCTAssertNotEqual(v.expect, "ok", v.name)
            default: XCTFail("\(v.name): unexpected \(outcome)")
            }
            await fulfillment(of: [ran], timeout: 0.5)
        }
    }

    func testACodeWorksOnceAndNonSafeActionsAreLeftAlone() async throws {
        let v = try XCTUnwrap(try vectors().first { $0.expect == "ok" && $0.fields?.scope == 0 })
        let fields = try XCTUnwrap(v.fields)
        let actions = CKeySafeActions(appID: fields.app, now: { Date(timeIntervalSince1970: v.now) })
            .trust(issuer: fields.issuer, keyID: fields.key_id, publicKeyHex: v.public_key)
            .register(fields.action, issuers: [fields.issuer]) { _ in }
        if case .performed = await actions.handle(v.payload) {} else { XCTFail("first use runs") }
        if case .rejected(let reason) = await actions.handle(v.payload) { XCTAssertTrue(reason.contains("already")) } else { XCTFail("second use is refused") }
        if case .notASafeAction = await actions.handle("https://citeory.com") {} else { XCTFail("ordinary links are not Safe Actions") }
    }

    func testCrossDeviceNeedsTheIssuerAndTheConfirmation() async throws {
        let v = try XCTUnwrap(try vectors().first { $0.expect == "ok" && $0.fields?.scope == 1 })
        let fields = try XCTUnwrap(v.fields)
        let ran = expectation(description: "must not run")
        ran.isInverted = true
        // No request_info: a cross-device action cannot run at all.
        let actions = CKeySafeActions(appID: fields.app, now: { Date(timeIntervalSince1970: v.now) })
            .trust(issuer: fields.issuer, keyID: fields.key_id, publicKeyHex: v.public_key)
            .register(fields.action, issuers: [fields.issuer], scope: .crossDevice) { _ in ran.fulfill() }
        if case .rejected = await actions.handle(v.payload) {} else { XCTFail("refused without the issuer's request information") }
        await fulfillment(of: [ran], timeout: 0.5)
    }
}
