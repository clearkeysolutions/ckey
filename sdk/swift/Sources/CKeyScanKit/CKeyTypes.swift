import CKeyCodec
import Foundation

/// An error reported by the CKey core, with its diagnostic message.
public struct CKeyError: Error, Equatable, CustomStringConvertible {
    /// The core's status codes (`ckey_status`), plus configuration errors
    /// raised by the Swift layer itself.
    public enum Code: Int, Sendable {
        case invalidArgument = 1
        case unsupportedProfile = 2
        case insufficientBuffer = 3
        case internalError = 4
        case uncorrectable = 5
        case integrityFailure = 6
        /// An image could not be read or created (Core Graphics).
        case imageConversion = 100
        /// A configuration value is out of range (for example a telemetry
        /// mode other than 0, 1 or 2).
        case invalidConfiguration = 101
        /// Camera access was denied or restricted by the user or by policy.
        case cameraAccessDenied = 102
        /// No usable camera, or it could not be configured or started.
        case cameraUnavailable = 103
    }

    public let code: Code
    public let message: String

    public init(_ code: Code, _ message: String) {
        self.code = code
        self.message = message
    }

    public var description: String { "CKeyError(\(code)): \(message)" }

    /// Wraps a non-OK core status. Internal plumbing shared by the CKey module.
    public static func core(_ status: ckey_status, _ diagnostics: ckey_diagnostics? = nil,
                            _ fallback: String = "") -> CKeyError {
        let code = Code(rawValue: Int(status.rawValue)) ?? .internalError
        var text = fallback
        if let diagnostics {
            let message = CKeyError.string(fromDiagnostics: diagnostics)
            if !message.isEmpty { text = message }
        }
        if text.isEmpty { text = "CKey core status \(status.rawValue)" }
        return CKeyError(code, text)
    }

    static func string(fromDiagnostics diagnostics: ckey_diagnostics) -> String {
        withUnsafeBytes(of: diagnostics.message) { raw -> String in
            let bytes = raw.bindMemory(to: UInt8.self)
            let end = bytes.firstIndex(of: 0) ?? bytes.count
            return String(decoding: bytes[0..<end], as: UTF8.self)
        }
    }
}

/// Profile 0 layout family.
public enum CKeyLayout: Int, Sendable, CaseIterable {
    /// Format 0.27: the default screen and print layout.
    case standard = 0
    /// Format 0.28 ("P0.1"): larger marks and gaps for small prints, at a
    /// lower capacity per version.
    case print = 1
}

/// The optical profile a symbol was read as.
public enum CKeyProfile: Int, Sendable, CaseIterable {
    /// Profile 0, the scalable family (versions 1 to 31). See `CKeyResult.layout`.
    case scalable = 0
    /// Profile 1: a 16-byte locator.
    case locator = 1
    /// Profile 2: text, up to 200 bytes.
    case text = 2
    /// Profile 3: extended, up to 2,048 bytes.
    case extended = 3
}

/// A decoded symbol.
public struct CKeyResult: Equatable, Sendable {
    /// The payload bytes, verified by Reed-Solomon correction and CRC-32C.
    public let bytes: Data
    /// The payload as text when it is valid UTF-8 without control characters
    /// other than tab, LF and CR (spec/CKey-Payload-Conventions.md); `nil`
    /// for binary payloads.
    public let text: String?
    public let profile: CKeyProfile
    /// Profile 0 version (1...31); 0 for Profiles 1 to 3.
    public let version: Int
    /// Profile 0 layout; `nil` for Profiles 1 to 3.
    public let layout: CKeyLayout?

    public init(bytes: Data, profile: CKeyProfile, version: Int = 0, layout: CKeyLayout? = nil) {
        self.bytes = bytes
        self.text = CKeyPayload.text(from: bytes)
        self.profile = profile
        self.version = version
        self.layout = layout
    }

    /// The telemetry profile code (`ckey_telemetry_profile`): 0 = P0,
    /// 1 = P0.1 (print layout), 2 = P1, 3 = P2, 4 = P3.
    public var telemetryProfileCode: UInt32 {
        switch profile {
        case .scalable: return layout == .print ? 1 : 0
        case .locator: return 2
        case .text: return 3
        case .extended: return 4
        }
    }

    /// A short human-readable profile name, e.g. "Profile 0 · v3".
    public var profileDescription: String {
        switch profile {
        case .scalable: return layout == .print ? "Profile 0.1 · print · v\(version)" : "Profile 0 · v\(version)"
        case .locator: return "Profile 1 · Locator"
        case .text: return "Profile 2 · Text"
        case .extended: return "Profile 3 · Extended"
        }
    }
}

/// Versions of the linked core.
public enum CKeyCoreInfo {
    /// The core's software version, e.g. "0.1.0-build.63" (the repository's VERSION).
    public static var softwareVersion: String {
        ckey_get_software_version().map { String(cString: $0) } ?? "0.0.0"
    }

    /// The C ABI version of the linked core.
    public static var abiVersion: UInt32 { ckey_get_abi_version() }
}
