import Foundation

/// Receives scan-session events from `CKeyCameraScanner`, on the scanner's
/// frame queue (begin and end on the main thread). Implementations must be
/// fast and non-blocking: they run on the decode path.
///
/// The `CKey` product installs its aggregate telemetry through this protocol
/// (see `CKeyScanner` there). The scan-only `CKeyScanner` product never
/// installs one, so it carries no telemetry.
public protocol CKeyScanObserver: AnyObject {
    /// The host started scanning (a new scan session).
    func scanSessionDidBegin()
    /// One frame was processed in `milliseconds`.
    func scanFrameProcessed(milliseconds: Double)
    /// The session's first valid decode.
    func scanSessionDidDecode(_ result: CKeyResult)
    /// The host stopped scanning.
    func scanSessionDidEnd()
}
