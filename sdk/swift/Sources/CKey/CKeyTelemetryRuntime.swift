import CKeyCodec
import CKeyScanKit
import CKeyTelemetry
import Foundation
#if canImport(UIKit) && !os(watchOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

/// Monotonic milliseconds (includes time asleep), the one clock every
/// telemetry call uses.
func ckeyMonotonicMilliseconds() -> UInt64 {
    clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1_000_000
}

/// Records scan sessions in the telemetry core. The recording calls are a
/// few counter updates under the core's mutex: safe on the scan queue.
///
/// `CKeyScanner` uses this automatically. A host with its own camera
/// pipeline (calling `CKeyDecoder` directly) can report its sessions here
/// so they are counted the same way:
///
///     CKeyTelemetrySession.shared.scanSessionDidBegin()   // camera view appeared
///     CKeyTelemetrySession.shared.scanFrameProcessed(milliseconds: ms)
///     CKeyTelemetrySession.shared.scanSessionDidDecode(result)
///     CKeyTelemetrySession.shared.scanSessionDidEnd()     // camera view went away
///
/// In mode 0 every call is a no-op in the core.
public final class CKeyTelemetrySession: CKeyScanObserver {
    public static let shared = CKeyTelemetrySession()

    private init() {}

    public func scanSessionDidBegin() {
        CKeyTelemetryRuntime.shared.start()
        ckey_telemetry_session_begin(ckeyMonotonicMilliseconds())
    }

    public func scanFrameProcessed(milliseconds: Double) {
        let ms = milliseconds.isFinite ? UInt32(min(max(0, milliseconds.rounded()), Double(UInt32.max))) : 0
        ckey_telemetry_frame(ckeyMonotonicMilliseconds(), ms)
    }

    public func scanSessionDidDecode(_ result: CKeyResult) {
        _ = ckey_telemetry_session_success(ckeyMonotonicMilliseconds(), result.telemetryProfileCode)
    }

    public func scanSessionDidEnd() {
        ckey_telemetry_session_end(ckeyMonotonicMilliseconds())
        CKeyTelemetryRuntime.shared.sendIfDue()
    }
}

/// The continuous camera scanner of the `CKey` product: `CKeyCameraScanner`
/// with telemetry sessions attached (a session begins at `start()` and
/// `scanAgain()`, succeeds at the first valid decode, and ends at `stop()`).
/// It follows `CKey.telemetry`; in mode 0 nothing is counted or sent.
///
///     let scanner = CKeyScanner()
///     scanner.onResult = { result in show(result) }
///     scanner.start()
#if canImport(AVFoundation) && (os(iOS) || os(macOS))
public final class CKeyScanner: CKeyCameraScanner {
    public init() {
        super.init(observer: CKeyTelemetrySession.shared)
    }
}
#endif

/// Sends aggregate batches. Never runs on the scan queue and never blocks:
/// all work happens on a private utility queue and in URLSession callbacks.
final class CKeyTelemetryRuntime {
    static let shared = CKeyTelemetryRuntime()

    private let queue = DispatchQueue(label: "org.openckey.telemetry", qos: .utility)
    private var started = false
    private var timer: DispatchSourceTimer?
    private var observers: [NSObjectProtocol] = []
    private lazy var session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.httpCookieAcceptPolicy = .never
        configuration.httpShouldSetCookies = false
        configuration.urlCache = nil
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.timeoutIntervalForRequest = 30
        configuration.timeoutIntervalForResource = 60
        configuration.waitsForConnectivity = false
        return URLSession(configuration: configuration)
    }()
    private let dayFormatter: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withFullDate]
        formatter.timeZone = TimeZone(identifier: "UTC")
        return formatter
    }()

    private init() {}

    /// Idempotent. Sets the client identity, starts a low-frequency check
    /// and listens for the app moving to the background.
    func start() {
        queue.async { [self] in
            guard !started else { return }
            started = true
            #if os(iOS)
            let platform = "ios"
            #elseif os(macOS)
            let platform = "macos"
            #else
            let platform = "other"
            #endif
            _ = ckey_telemetry_set_client("swift", CKeyCoreInfo.softwareVersion, platform)

            // should_send enforces the cadence (one batch per hour at most);
            // this timer only gives it a chance to run.
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now() + 120, repeating: 900, leeway: .seconds(120))
            timer.setEventHandler { [weak self] in self?.sendNow() }
            timer.resume()
            self.timer = timer

            #if canImport(UIKit) && !os(watchOS)
            let background: Notification.Name? = UIApplication.didEnterBackgroundNotification
            #elseif os(macOS)
            let background: Notification.Name? = NSApplication.didResignActiveNotification
            #else
            let background: Notification.Name? = nil
            #endif
            if let background {
                let token = NotificationCenter.default.addObserver(forName: background, object: nil, queue: nil) { [weak self] _ in
                    self?.sendInBackground()
                }
                observers.append(token)
            }
        }
    }

    /// Asynchronously tries a send if the core says one is due.
    func sendIfDue() {
        queue.async { [weak self] in self?.sendNow() }
    }

    private func sendInBackground() {
        #if os(iOS)
        // A short expiring-activity assertion lets the request leave before
        // suspension; it works in apps and app extensions alike.
        ProcessInfo.processInfo.performExpiringActivity(withReason: "CKey aggregate telemetry") { [weak self] expired in
            guard !expired, let self else { return }
            let done = DispatchSemaphore(value: 0)
            self.queue.async { self.sendNow { done.signal() } }
            _ = done.wait(timeout: .now() + 25)
        }
        #else
        sendIfDue()
        #endif
    }

    /// On `queue`. `completion` runs once the attempt (if any) has finished.
    private func sendNow(completion: (() -> Void)? = nil) {
        let now = ckeyMonotonicMilliseconds()
        guard ckey_telemetry_should_send(now) != 0 else { completion?(); return }
        let day = dayFormatter.string(from: Date())
        var json = [CChar](repeating: 0, count: Int(CKEY_TELEMETRY_MAX_BODY_BYTES) + 1)
        var length = 0
        var batchID: UInt64 = 0
        let status = json.withUnsafeMutableBufferPointer { buffer in
            ckey_telemetry_take_batch(day, buffer.baseAddress, buffer.count, &length, &batchID)
        }
        guard status == CKEY_OK, batchID != 0, length > 0, length < json.count,
              let url = URL(string: CKEY_TELEMETRY_ENDPOINT) else {
            if batchID != 0 { _ = ckey_telemetry_batch_result(batchID, 0, ckeyMonotonicMilliseconds()) }
            completion?()
            return
        }
        let body = Data(json[0..<length].map { UInt8(bitPattern: $0) })
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = body
        request.httpShouldHandleCookies = false
        let task = session.dataTask(with: request) { [queue] _, response, _ in
            // 0 = network error, timeout or no connectivity: the core keeps the batch.
            let http = (response as? HTTPURLResponse)?.statusCode ?? 0
            queue.async {
                _ = ckey_telemetry_batch_result(batchID, Int32(clamping: http), ckeyMonotonicMilliseconds())
                completion?()
            }
        }
        task.resume()
    }
}
