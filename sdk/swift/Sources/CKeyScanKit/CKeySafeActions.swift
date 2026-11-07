import CKeyCodec
import Foundation
#if canImport(SwiftUI)
import SwiftUI
#endif

/// Where a Safe Action takes effect (spec/CKey-Safe-Actions.md).
public enum CKeySafeActionScope: Sendable, Equatable {
    /// The result stays in this app on this device: it runs at once, with no prompt.
    case sameDevice
    /// It approves something elsewhere, such as signing in a browser: it runs only
    /// after the SDK's own confirmation.
    case crossDevice
}

/// A Safe Action that verified: issuer, app, action and parameters are exactly as signed.
public struct CKeyVerifiedSafeAction: Sendable, Equatable {
    public let issuer: String
    public let app: String
    public let action: String
    public let keyID: String
    public let requestID: String
    public let scope: CKeySafeActionScope
    public let issuedAt: Date
    public let expiresAt: Date
    /// The signed parameters, normally UTF-8 JSON.
    public let params: Data
    public var paramsText: String? { String(data: params, encoding: .utf8) }
}

/// Information the issuer gives about a cross-device request, shown in the confirmation.
public struct CKeySafeActionRequestInfo: Sendable, Equatable {
    public let device: String?
    public let location: String?
    public let requestedAt: String?
}

/// A verified cross-device Safe Action waiting for the person's approval.
///
/// The handler runs only through the SDK's confirmation (`CKeySafeActionPrompt`);
/// approval is not part of this type's public interface.
public final class CKeySafeActionRequest: Identifiable, @unchecked Sendable {
    public let id = UUID()
    public let title: String
    public let details: CKeyVerifiedSafeAction
    public let requestInfo: CKeySafeActionRequestInfo
    private let handler: @Sendable (CKeyVerifiedSafeAction) async -> Void
    private let lock = NSLock()
    private var decided = false

    init(title: String, details: CKeyVerifiedSafeAction, requestInfo: CKeySafeActionRequestInfo,
         handler: @escaping @Sendable (CKeyVerifiedSafeAction) async -> Void) {
        self.title = title
        self.details = details
        self.requestInfo = requestInfo
        self.handler = handler
    }

    private func locked<T>(_ body: () -> T) -> T { lock.lock(); defer { lock.unlock() }; return body() }

    /// Internal: called only by the SDK's confirmation view.
    func decide(approved: Bool) async {
        let first: Bool = locked {
            defer { decided = true }
            return !decided
        }
        guard first, approved else { return }
        await handler(details)
    }
}

/// What `CKeySafeActions.handle(_:)` did with a decoded payload.
public enum CKeySafeActionOutcome: Sendable {
    /// Not a Safe Action: follow the ordinary payload rules (show it; act on a tap).
    case notASafeAction
    /// It claims to be a Safe Action but did not verify. Nothing ran; show the reason.
    case rejected(reason: String)
    /// A verified same-device action; its handler has run.
    case performed(CKeyVerifiedSafeAction)
    /// A verified cross-device action; present `CKeySafeActionPrompt(request:)`.
    case needsConfirmation(CKeySafeActionRequest)
}

/// Safe Actions for this app. The SDK decides whether an action runs: same-device
/// actions run at once; cross-device actions run only after the SDK's confirmation;
/// anything that does not verify runs nothing.
///
/// ```swift
/// let actions = CKeySafeActions(appID: "com.viadere")
/// actions.trust(issuer: "citeory.com", keyID: "2026-11", publicKeyHex: "…",
///               requestInfo: "https://citeory.com/api/ckey/requests/{request_id}")
/// actions.register("signin.request", issuers: ["citeory.com"], scope: .crossDevice) { action in … }
/// switch await actions.handle(result.text ?? "") { … }
/// ```
public final class CKeySafeActions: @unchecked Sendable {
    public static let prefix = "ckey-action:1:"

    private struct Key { let bytes: [UInt8]; let requestInfo: String? }
    private struct Registration { let issuers: Set<String>; let scope: CKeySafeActionScope; let title: String?; let handler: @Sendable (CKeyVerifiedSafeAction) async -> Void }

    public let appID: String
    private let maxLifetime: UInt32
    private let session: URLSession
    private let now: @Sendable () -> Date
    private let lock = NSLock()
    private var keys: [String: Key] = [:]
    private var actions: [String: Registration] = [:]
    private var seen: [String: Date] = [:]

    /// - Parameters:
    ///   - maxLifetime: the longest lifetime this app accepts, 1...3600 seconds.
    public init(appID: String, maxLifetime: UInt32 = 600, session: URLSession = .shared,
                now: @escaping @Sendable () -> Date = { Date() }) {
        precondition(appID.range(of: "^[A-Za-z0-9._-]{1,255}$", options: .regularExpression) != nil, "appID must be an app ID such as com.example.app")
        precondition((1...3600).contains(maxLifetime), "maxLifetime is 1 to 3600 seconds")
        self.appID = appID
        self.maxLifetime = maxLifetime
        self.session = session
        self.now = now
    }

    private func locked<T>(_ body: () -> T) -> T { lock.lock(); defer { lock.unlock() }; return body() }

    public static func isSafeAction(_ text: String) -> Bool { text.hasPrefix(prefix) && text.count > prefix.count }

    /// Pins an issuer's Ed25519 public key (64 hex digits). `requestInfo` is the
    /// issuer's request_info URL template, required for cross-device actions.
    @discardableResult
    public func trust(issuer: String, keyID: String, publicKeyHex: String, requestInfo: String? = nil) -> Self {
        precondition(publicKeyHex.range(of: "^[0-9A-Fa-f]{64}$", options: .regularExpression) != nil, "a public key is 64 hexadecimal digits")
        let digits = Array(publicKeyHex.utf8)
        let bytes = stride(from: 0, to: 64, by: 2).map { i -> UInt8 in
            UInt8(String(decoding: digits[i..<(i + 2)], as: UTF8.self), radix: 16)!
        }
        locked { keys[issuer + " " + keyID] = Key(bytes: bytes, requestInfo: requestInfo) }
        return self
    }

    /// Registers an action this app accepts. The stricter of this scope and the
    /// envelope's own scope applies.
    @discardableResult
    public func register(_ action: String, issuers: [String], scope: CKeySafeActionScope = .sameDevice, title: String? = nil,
                         handler: @escaping @Sendable (CKeyVerifiedSafeAction) async -> Void) -> Self {
        precondition(action.range(of: "^[a-z0-9._-]{1,64}$", options: .regularExpression) != nil, "action is 1 to 64 of a-z 0-9 . _ -")
        precondition(!issuers.isEmpty, "name the issuers allowed to request this action")
        locked { actions[action] = Registration(issuers: Set(issuers), scope: scope, title: title, handler: handler) }
        return self
    }

    /// Handles a decoded payload.
    public func handle(_ text: String) async -> CKeySafeActionOutcome {
        guard Self.isSafeAction(text) else { return .notASafeAction }
        let bytes = Array(text.utf8)
        var parsed = ckey_safe_action()
        var diag = ckey_diagnostics()
        guard ckey_safe_action_parse(bytes, bytes.count, &parsed, &diag) == CKEY_OK else { return .rejected(reason: message(diag)) }
        let claimed = Self.fields(parsed)
        guard claimed.app == appID else { return .rejected(reason: "this code is for another app (\(claimed.app))") }
        let state: (Registration?, Key?) = locked { (actions[claimed.action], keys[claimed.issuer + " " + claimed.keyID]) }
        guard let registration = state.0 else { return .rejected(reason: "this app does not accept the action \(claimed.action)") }
        guard registration.issuers.contains(claimed.issuer) else { return .rejected(reason: "\(claimed.issuer) may not request \(claimed.action)") }
        guard let key = state.1 else { return .rejected(reason: "no trusted key \(claimed.keyID) for \(claimed.issuer)") }
        var verified = ckey_safe_action()
        let status = key.bytes.withUnsafeBufferPointer { k in
            ckey_safe_action_verify(bytes, bytes.count, k.baseAddress, UInt64(now().timeIntervalSince1970), maxLifetime, &verified, &diag)
        }
        guard status == CKEY_OK else { return .rejected(reason: message(diag)) }
        let action = Self.fields(verified)
        let fresh: Bool = locked {
            let current = now()
            seen = seen.filter { $0.value > current }
            if seen[action.requestID] != nil { return false }
            seen[action.requestID] = action.expiresAt.addingTimeInterval(120)
            return true
        }
        guard fresh else { return .rejected(reason: "this code has already been used") }
        if action.scope == .sameDevice && registration.scope == .sameDevice {
            await registration.handler(action)
            return .performed(action)
        }
        guard let info = await requestInfo(template: key.requestInfo, issuer: action.issuer, requestID: action.requestID) else {
            return .rejected(reason: "the issuer did not confirm a pending request, so this cross-device action cannot run")
        }
        return .needsConfirmation(CKeySafeActionRequest(title: registration.title ?? "Continue with \(action.issuer)?", details: action,
                                                        requestInfo: info, handler: registration.handler))
    }

    private func requestInfo(template: String?, issuer: String, requestID: String) async -> CKeySafeActionRequestInfo? {
        guard let template, let encoded = requestID.addingPercentEncoding(withAllowedCharacters: .alphanumerics),
              let url = URL(string: template.replacingOccurrences(of: "{request_id}", with: encoded)),
              url.scheme == "https", let host = url.host, host == issuer || host.hasSuffix("." + issuer) else { return nil }
        var request = URLRequest(url: url)
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        guard let (data, response) = try? await session.data(for: request), (response as? HTTPURLResponse)?.statusCode == 200,
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              json["request_id"] as? String == requestID, json["status"] as? String == "pending" else { return nil }
        let short = { (v: Any?, max: Int) -> String? in (v as? String).flatMap { $0.isEmpty || $0.count > max ? nil : $0 } }
        return CKeySafeActionRequestInfo(device: short(json["device"], 120), location: short(json["location"], 120), requestedAt: short(json["requested_at"], 40))
    }

    private func message(_ diag: ckey_diagnostics) -> String {
        withUnsafeBytes(of: diag.message) { raw in String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self) }
    }

    private static func text<T>(_ field: T) -> String {
        withUnsafeBytes(of: field) { raw in String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self) }
    }

    static func fields(_ sa: ckey_safe_action) -> CKeyVerifiedSafeAction {
        let params = withUnsafeBytes(of: sa.params) { Data($0.prefix(Int(sa.params_len))) }
        return CKeyVerifiedSafeAction(issuer: text(sa.issuer), app: text(sa.app), action: text(sa.action), keyID: text(sa.key_id),
                                      requestID: text(sa.request_id), scope: sa.scope == 1 ? .crossDevice : .sameDevice,
                                      issuedAt: Date(timeIntervalSince1970: TimeInterval(sa.issued_at)),
                                      expiresAt: Date(timeIntervalSince1970: TimeInterval(sa.expires_at)), params: params)
    }
}

#if canImport(SwiftUI)
/// The SDK's confirmation for a cross-device Safe Action. Present it (for example
/// in a sheet); the action runs only if the person chooses Continue.
@available(iOS 15.0, macOS 12.0, *)
public struct CKeySafeActionPrompt: View {
    private let request: CKeySafeActionRequest
    private let onFinish: (Bool) -> Void
    @State private var busy = false

    public init(request: CKeySafeActionRequest, onFinish: @escaping (_ approved: Bool) -> Void = { _ in }) {
        self.request = request
        self.onFinish = onFinish
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(request.title).font(.title3.weight(.semibold))
            VStack(alignment: .leading, spacing: 6) {
                row("Requested by", request.details.issuer + " (verified)")
                if let device = request.requestInfo.device { row("Device", device) }
                if let location = request.requestInfo.location { row("Location", location) }
                if let time = request.requestInfo.requestedAt { row("Requested", time) }
            }
            Text("Only continue if this is the device and place you are using right now. Someone may be trying to use a code meant for their own session.")
                .font(.footnote).foregroundStyle(.secondary)
            HStack {
                Button("Cancel", role: .cancel) { finish(false) }
                Spacer()
                Button("Continue") { finish(true) }.buttonStyle(.borderedProminent)
            }
            .disabled(busy)
        }
        .padding(20)
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(label).foregroundStyle(.secondary).frame(minWidth: 96, alignment: .leading)
            Text(value)
        }
        .font(.subheadline)
    }

    private func finish(_ approved: Bool) {
        busy = true
        Task {
            await request.decide(approved: approved)
            onFinish(approved)
        }
    }
}
#endif
