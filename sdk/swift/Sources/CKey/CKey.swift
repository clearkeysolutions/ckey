@_exported import CKeyScanKit
import CKeyCodec
import CKeyTelemetry
import Foundation

/// Telemetry modes. The numeric values are canonical across every CKey SDK
/// (docs/adoption-and-telemetry.md).
public enum CKeyTelemetryMode: Int, Sendable, CaseIterable, ExpressibleByIntegerLiteral {
    /// 0: no counting, nothing sent. Switching to it purges every counter.
    case off = 0
    /// 1 (default): local aggregate counting; batches are sent to openckey.org
    /// at most once an hour, never on the scan path.
    case on = 1
    /// 2: local aggregate counting only; nothing is ever sent.
    case local = 2

    /// Allows `CKey.telemetry = 0`. Only the literals 0, 1 and 2 are valid; any
    /// other literal is a programming error and stops the program (it is
    /// never silently treated as 1). For values known only at run time, use
    /// `try CKey.setTelemetry(rawValue:)` or `CKeyTelemetryMode(rawValue:)`.
    public init(integerLiteral value: Int) {
        guard let mode = CKeyTelemetryMode(rawValue: value) else {
            fatalError("CKey telemetry mode must be 0 (off), 1 (on) or 2 (local); got \(value)")
        }
        self = mode
    }
}

/// Aggregate scan counters kept by the telemetry core (modes 1 and 2).
public struct CKeyTelemetryCounters: Equatable, Sendable {
    public struct Histogram: Equatable, Sendable {
        public let count: UInt64
        public let sum: UInt64
        /// Ten buckets with inclusive upper edges (see docs/adoption-and-telemetry.md).
        public let buckets: [UInt64]
    }
    public let sessionsStarted: UInt64
    public let sessionsSucceeded: UInt64
    public let sessionsFailed: UInt64
    public let frames: UInt64
    /// Decodes per profile, keyed "p0", "p0_1", "p1", "p2", "p3".
    public let decoded: [String: UInt64]
    public let timeToDecodeMilliseconds: Histogram
    public let frameMilliseconds: Histogram
}

/// Package-level configuration.
///
/// `CKey` is both this module's name and this type's name; inside an app,
/// `CKey.telemetry` refers to the type.
public enum CKey {
    /// The core's software version (the repository's VERSION).
    public static var version: String { CKeyCoreInfo.softwareVersion }

    /// The telemetry mode: `.on` (1, default), `.local` (2) or `.off` (0).
    /// Opting out is one line: `CKey.telemetry = .off` (or `= 0`).
    ///
    /// Setting a mode applies the core's switching rules: `.off` purges all
    /// counters; `.on` -> `.local` drops anything unsent (an in-flight batch's
    /// result is ignored). The mode is process-wide and not persisted; set it
    /// at launch, before scanning.
    public static var telemetry: CKeyTelemetryMode {
        get {
            CKeyTelemetryRuntime.shared.start()
            return CKeyTelemetryMode(rawValue: Int(ckey_telemetry_get_mode())) ?? .on
        }
        set {
            CKeyTelemetryRuntime.shared.start()
            // Every enum value is valid, so the core cannot refuse it.
            _ = ckey_telemetry_set_mode(Int32(newValue.rawValue))
        }
    }

    /// Sets the telemetry mode from a number (0, 1 or 2), e.g. from a
    /// configuration file.
    /// - Throws: `CKeyError` with code `.invalidConfiguration` for any other
    ///   value; the mode is left unchanged (never silently reset to 1).
    public static func setTelemetry(rawValue: Int) throws {
        guard let mode = CKeyTelemetryMode(rawValue: rawValue) else {
            throw CKeyError(.invalidConfiguration,
                            "telemetry mode must be 0 (off), 1 (on) or 2 (local); got \(rawValue)")
        }
        telemetry = mode
    }

    /// The local aggregate totals since counting started (all zero in mode 0).
    /// Available to the host in modes 1 and 2; nothing here identifies a
    /// user, a device or a payload.
    public static var telemetryCounters: CKeyTelemetryCounters {
        var c = ckey_telemetry_counters()
        _ = ckey_telemetry_snapshot(&c)
        func histogram(_ h: ckey_telemetry_histogram) -> CKeyTelemetryCounters.Histogram {
            let buckets = withUnsafeBytes(of: h.buckets) { Array($0.bindMemory(to: UInt64.self)) }
            return .init(count: h.count, sum: h.sum, buckets: buckets)
        }
        let decoded = withUnsafeBytes(of: c.decoded) { Array($0.bindMemory(to: UInt64.self)) }
        let keys = ["p0", "p0_1", "p1", "p2", "p3"]
        var byProfile: [String: UInt64] = [:]
        for (index, key) in keys.enumerated() where index < decoded.count { byProfile[key] = decoded[index] }
        return CKeyTelemetryCounters(sessionsStarted: c.sessions_started, sessionsSucceeded: c.sessions_succeeded,
                                     sessionsFailed: c.sessions_failed, frames: c.frames, decoded: byProfile,
                                     timeToDecodeMilliseconds: histogram(c.time_to_decode_ms),
                                     frameMilliseconds: histogram(c.frame_ms))
    }
}
