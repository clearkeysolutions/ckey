#if canImport(AVFoundation) && (os(iOS) || os(macOS))
import AVFoundation
import Combine
import CoreVideo
import Foundation
import QuartzCore
#if canImport(SwiftUI)
import SwiftUI
#endif
#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

/// A continuous AVFoundation camera scanner that owns the camera: it picks
/// the capture device, runs the session, decodes live frames, tracks the code
/// on every frame and drives focus, exposure and zoom from the core's camera
/// advice. The first valid decode of a session goes to `onResult` on the main
/// queue; decoding then pauses until `scanAgain()`, while tracking continues.
///
/// **The scanner takes control of the camera by default.** Each part has a
/// flag, all on by default:
///
/// | Flag | What it does |
/// |---|---|
/// | `controlsCamera` | Master switch for `focusOnCode`, `exposureControl` and `realDetailZoom`. |
/// | `focusOnCode` | Continuous autofocus, near range, aimed at the code. |
/// | `exposureControl` | Exposure metered on the code, with the core's exposure bias. |
/// | `realDetailZoom` | Zoom on a small code, only as far as the sensor gives real detail. |
/// | `regionOfInterest` | Two of every three frames decode only `scanRegion` plus a margin. |
/// | `tracking` | Per-frame tracking, published through `onTrack`. |
/// | `keepsScreenAwake` | No auto-lock while the camera runs (iOS apps). |
///
/// A host that manages its own capture device turns `controlsCamera` off, or
/// uses `CKeyFrameProcessor` (and optionally `CKeyCameraControl`) with its
/// own session instead of this class.
///
/// Add `NSCameraUsageDescription` to the app's Info.plist. On macOS, sandboxed
/// apps also need the camera entitlement. Call `start()`, `stop()` and
/// `scanAgain()` from the main thread.
///
/// The `CKey` product's `CKeyScanner` is this class with telemetry attached;
/// apps that link only the scan-only product use this class directly.
open class CKeyCameraScanner: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    /// The capture session; show it with `CKeyCameraPreview` or an
    /// `AVCaptureVideoPreviewLayer`.
    public let session = AVCaptureSession()
    /// Called on the main queue with the first valid decode of a session.
    public var onResult: ((CKeyResult) -> Void)?
    /// Called on the main queue when the camera cannot be used.
    public var onError: ((CKeyError) -> Void)?
    /// Frame-rate cap for decoding; stale frames are discarded. Tracking
    /// runs on every frame regardless.
    public var maxFramesPerSecond: Double {
        get { processor.maxFramesPerSecond }
        set { processor.maxFramesPerSecond = newValue }
    }

    // MARK: Camera control (on by default)

    /// The master switch: the SDK drives focus, exposure and zoom from the
    /// core's advice (default on). Off leaves the device alone after the
    /// initial setup (continuous autofocus and auto exposure, and the wide
    /// lens's field of view on a multi-camera device), whatever the three
    /// flags below say. Region decoding and tracking do not touch the device
    /// and have their own flags.
    public var controlsCamera = true { didSet { updateControlFlags() } }
    /// Continuous autofocus, restricted to the near range where supported,
    /// aimed at the reticle and then at the found code; re-aimed only when
    /// the code moves (default on).
    public var focusOnCode = true { didSet { updateControlFlags() } }
    /// Exposure metered on the code, with a negative bias when the code
    /// clips and a 0 / -1 / -2 EV cycle while nothing decodes (default on).
    public var exposureControl = true { didSet { updateControlFlags() } }
    /// Zoom on a small code, only up to the active format's
    /// `videoZoomFactorUpscaleThreshold`, where the sensor is cropped at
    /// full resolution, or to a telephoto lens (default on).
    public var realDetailZoom = true { didSet { updateControlFlags() } }

    /// The control that drives the capture device, for its finer settings
    /// (`closeFocusZoom`, `distortionCorrection`, `typicalCodeWidth`); `nil`
    /// when there is no camera. Its three main flags are set by this scanner.
    public let cameraControl: CKeyCameraControl?
    /// The camera this scanner uses (see `CKeyCameraControl.preferredDevice()`),
    /// for example for the torch; `nil` when there is none.
    public var captureDevice: AVCaptureDevice? { cameraControl?.device }

    // MARK: Region of interest

    /// Region decoding (default on): two of every three analysed frames
    /// decode only `scanRegion` plus an 18% margin, at full resolution and in
    /// place; every third frame is decoded whole, so a code outside the
    /// region or larger than it still reads.
    public var regionOfInterest: Bool {
        get { processor.regionOfInterest }
        set { processor.regionOfInterest = newValue }
    }

    /// Where the code is expected, in capture-device coordinates (fractions
    /// of the sensor frame): the reticle, converted with
    /// `CKeyPreviewView.sensorRect(fromViewRect:)`. Decoding reads it first,
    /// and focus and exposure rest on its centre. The default is the middle
    /// of the frame; `nil` decodes whole frames only.
    public var scanRegion: CGRect? {
        get { processor.scanRegion }
        set {
            processor.scanRegion = newValue
            guard let region = processor.scanRegion, let control = cameraControl else { return }
            let centre = CGPoint(x: region.midX, y: region.midY)
            let previous = control.restingPoint
            guard abs(previous.x - centre.x) > 0.002 || abs(previous.y - centre.y) > 0.002 else { return }
            control.restingPoint = centre
            control.aimAtRest()
        }
    }

    /// Multi-frame reading (default on): frames that do not decode are
    /// averaged per symbol and the average is decoded, so frames that are
    /// each too noisy, glared or partly hidden can read together. See
    /// `CKeyFrameProcessor.multiFrame`.
    public var multiFrame: Bool {
        get { processor.multiFrame }
        set { processor.multiFrame = newValue }
    }

    // MARK: Camera access and torch

    /// Whether the app may use the camera, for the UI to explain or ask.
    public enum CameraAuthorization: Equatable, Sendable {
        /// The person has not been asked yet.
        case notDetermined
        case authorized
        /// The person said no; they can change it in Settings.
        case denied
        /// Camera use is blocked by Screen Time or device management.
        case restricted
        /// There is no suitable camera (for example the Simulator).
        case unavailable
    }

    /// The current camera authorization, or `.unavailable` without a camera.
    public var cameraAuthorization: CameraAuthorization {
        guard cameraControl != nil else { return .unavailable }
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized: return .authorized
        case .notDetermined: return .notDetermined
        case .denied: return .denied
        case .restricted: return .restricted
        @unknown default: return .denied
        }
    }

    /// Asks for camera access if it has not been decided yet; returns the
    /// resulting authorization. It does not start the camera.
    public func requestCameraAccess() async -> CameraAuthorization {
        if cameraAuthorization == .notDetermined {
            _ = await AVCaptureDevice.requestAccess(for: .video)
        }
        return cameraAuthorization
    }

    /// True when the scanner's camera has a torch.
    public var isTorchAvailable: Bool { captureDevice?.hasTorch ?? false }

    /// Turns the torch on or off while the camera runs; returns whether it is
    /// now on. Stopping the camera turns it off.
    @discardableResult
    public func setTorch(_ on: Bool) -> Bool {
        guard let device = captureDevice, device.hasTorch, isStarted else { return false }
        do {
            try device.lockForConfiguration()
            defer { device.unlockForConfiguration() }
            let mode: AVCaptureDevice.TorchMode = on ? .on : .off
            guard device.isTorchModeSupported(mode) else { return device.torchMode == .on }
            device.torchMode = mode
            return on
        } catch {
            return false
        }
    }

    // MARK: Tracking and guidance

    /// Per-frame tracking (default on): the code's four corners on every
    /// frame, measured by the core on a small crop around the last corners,
    /// on its own queue and separately from decoding. It keeps going after a
    /// read. Tracks are published through `onTrack` whether or not an overlay
    /// is shown.
    public var tracking: Bool {
        get { processor.tracking }
        set { processor.tracking = newValue }
    }

    /// The raw track, for custom overlays: called on `trackQueue` for every
    /// frame the code was measured on, and with `nil` once the track is lost.
    /// `CKeyBracketOverlay` is the built-in drawing of the same data.
    public var onTrack: ((CKeyTrack?) -> Void)? {
        get { processor.onTrack }
        set { processor.onTrack = newValue }
    }

    /// Diagnostics: a report on every analysed frame with the camera's state,
    /// on the main queue. Leave nil in production (it costs a histogram and a
    /// main-queue hop per frame).
    public var onFrameReport: ((CKeyFrameReport, CameraState) -> Void)? {
        didSet {
            guard let handler = onFrameReport else { processor.onFrameReport = nil; return }
            let device = cameraControl?.device
            processor.onFrameReport = { report in
                #if os(iOS)
                let state = CameraState(exposureSeconds: device.map { CMTimeGetSeconds($0.exposureDuration) }, iso: device?.iso,
                                        lensPosition: device?.lensPosition, zoomFactor: device.map { Double($0.videoZoomFactor) },
                                        deviceName: device?.localizedName)
                #else
                let state = CameraState(exposureSeconds: nil, iso: nil, lensPosition: nil, zoomFactor: nil, deviceName: device?.localizedName)
                #endif
                DispatchQueue.main.async { handler(report, state) }
            }
        }
    }

    /// The queue `onTrack` is called on (default main).
    public var trackQueue: DispatchQueue {
        get { processor.trackQueue }
        set { processor.trackQueue = newValue }
    }

    /// Called on the main queue when what the user should be told changes:
    /// move closer, move back (a corner is cut off), hold steady, refocus,
    /// glare. An empty set clears the hint.
    public var onGuidance: ((CKeyGuidance) -> Void)? {
        get { processor.onGuidance }
        set { processor.onGuidance = newValue }
    }

    // MARK: Screen

    /// Keeps the screen awake while the camera runs (default on), so the
    /// device does not auto-lock mid-scan: `UIApplication.isIdleTimerDisabled`
    /// is set at `start()` and cleared at `stop()`. iOS apps only; it does
    /// nothing in app extensions and on macOS. Turn it off if the app manages
    /// the idle timer itself.
    public var keepsScreenAwake = true {
        didSet { if keepsScreenAwake != oldValue, isStarted { setScreenAwake(keepsScreenAwake) } }
    }

    private let observer: CKeyScanObserver?
    private let processor: CKeyFrameProcessor
    private let sessionQueue = DispatchQueue(label: "org.openckey.scanner.session")
    private let frameQueue = DispatchQueue(label: "org.openckey.scanner.frames", qos: .userInitiated)
    private let output = AVCaptureVideoDataOutput()
    private var configured = false
    // Main-thread state.
    private var sessionOpen = false
    private var isStarted = false
    private var heldScreenAwake = false

    /// Capture resolution. `.uhd4K` reads the densest symbols; `.hd1080` uses
    /// far less memory (camera extensions, older devices).
    public enum Resolution: Sendable { case uhd4K, hd1080 }
    private let resolution: Resolution

    /// - Parameters:
    ///   - observer: optional session observer (for example your own metrics);
    ///     held strongly. `nil` for none.
    ///   - resolution: preferred capture resolution; falls back to the best
    ///     the device supports.
    ///   - device: the camera to use; `nil` (default) picks
    ///     `CKeyCameraControl.preferredDevice()`.
    public init(observer: CKeyScanObserver?, resolution: Resolution = .uhd4K, device: AVCaptureDevice? = nil) {
        self.observer = observer
        self.resolution = resolution
        processor = CKeyFrameProcessor(observer: observer)
        cameraControl = (device ?? CKeyCameraControl.preferredDevice()).map { CKeyCameraControl(device: $0) }
        super.init()
        processor.onResult = { [weak self] result in self?.onResult?(result) }
        if let control = cameraControl {
            let processor = self.processor
            processor.onAdvice = { [weak control] advice in control?.apply(advice) }
            control.onZoomChange = { [weak processor] ratio in processor?.zoomDidChange(by: ratio) }
            if let region = processor.scanRegion {
                control.restingPoint = CGPoint(x: region.midX, y: region.midY)
            }
        }
    }

    deinit {
        if sessionOpen { observer?.scanSessionDidEnd() }
        if heldScreenAwake { CKeyCameraScanner.setIdleTimerDisabled(false) }
        let session = self.session
        sessionQueue.async { if session.isRunning { session.stopRunning() } }
    }

    /// Requests camera access if needed, then starts the camera and a scan session.
    public func start() {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized:
            run()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] allowed in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if allowed { self.run() } else { self.fail("Camera access was denied.", .cameraAccessDenied) }
                }
            }
        default:
            fail("Camera access is not authorized.", .cameraAccessDenied)
        }
    }

    /// Stops the camera and ends the scan session (a session without a decode fails).
    public func stop() {
        endSession()
        isStarted = false
        setScreenAwake(false)
        processor.reset()
        let session = self.session
        sessionQueue.async { if session.isRunning { session.stopRunning() } }
    }

    /// Starts a new scan session after a decode, keeping the camera running.
    /// The track is dropped and the zoom and exposure return to rest.
    /// The camera's state when asked, for diagnostics (all optional).
    public struct CameraState: Sendable {
        public let exposureSeconds: Double?
        public let iso: Float?
        public let lensPosition: Float?
        public let zoomFactor: Double?
        public let deviceName: String?
    }

    /// Keeps the next `count` analysed frames for diagnosis
    /// (`CKeyFrameProcessor.captureDiagnostics`), with the camera's state.
    public func captureDiagnostics(count: Int = 12,
                                   completion: @escaping @Sendable ([CKeyDiagnosticFrame], CameraState) -> Void) {
        // The camera's state when the capture starts (read here: the device
        // is not Sendable, so the completion does not capture it).
        let device = cameraControl?.device
        #if os(iOS)
        let state = CameraState(exposureSeconds: device.map { CMTimeGetSeconds($0.exposureDuration) }, iso: device?.iso,
                                lensPosition: device?.lensPosition, zoomFactor: device.map { Double($0.videoZoomFactor) },
                                deviceName: device?.localizedName)
        #else
        let state = CameraState(exposureSeconds: device.map { CMTimeGetSeconds($0.exposureDuration) }, iso: nil,
                                lensPosition: nil, zoomFactor: nil, deviceName: device?.localizedName)
        #endif
        processor.captureDiagnostics(count: count) { frames in completion(frames, state) }
    }

    public func scanAgain() {
        endSession()
        beginSession()
        processor.scanAgain()
        cameraControl?.reset()
    }

    /// When a frame was captured, in host-clock seconds: its presentation
    /// timestamp converted from the session's synchronization clock. Tracks
    /// carry this time so an overlay can time its motion by when the camera
    /// saw the code, not by when the result arrived. Hosts with their own
    /// session pass it to `CKeyFrameProcessor.process(pixelBuffer:captureTime:)`.
    public static func captureTime(of sampleBuffer: CMSampleBuffer, session: AVCaptureSession) -> TimeInterval {
        let host = CMClockGetHostTimeClock()
        let clock: CMClock
        if #available(iOS 15.4, macOS 12.3, *) {
            clock = session.synchronizationClock ?? host
        } else {
            clock = session.masterClock ?? host
        }
        let seconds = CMTimeGetSeconds(CMSyncConvertTime(CMSampleBufferGetPresentationTimeStamp(sampleBuffer),
                                                         from: clock, to: host))
        return seconds.isFinite && seconds > 0 ? seconds : CACurrentMediaTime()
    }

    private func beginSession() {
        if !Thread.isMainThread { DispatchQueue.main.async { self.beginSession() }; return }
        sessionOpen = true
        observer?.scanSessionDidBegin()
    }

    private func endSession() {
        if !Thread.isMainThread { DispatchQueue.main.async { self.endSession() }; return }
        guard sessionOpen else { return }
        sessionOpen = false
        observer?.scanSessionDidEnd()
    }

    private func fail(_ message: String, _ code: CKeyError.Code = .cameraUnavailable) {
        DispatchQueue.main.async {
            self.isStarted = false
            self.setScreenAwake(false)
            self.onError?(CKeyError(code, message))
        }
    }

    private func run() {
        if !sessionOpen { beginSession() }
        isStarted = true
        setScreenAwake(keepsScreenAwake)
        processor.scanAgain()
        sessionQueue.async { [weak self] in
            guard let self else { return }
            if !self.configured {
                guard self.configure() else {
                    self.fail("Could not configure the camera.")
                    return
                }
                self.configured = true
            }
            // Zoom limits depend on the format the session settled on.
            self.cameraControl?.prepare()
            if !self.session.isRunning { self.session.startRunning() }
        }
    }

    private func configure() -> Bool {
        session.beginConfiguration()
        defer { session.commitConfiguration() }
        guard let camera = cameraControl?.device, let input = try? AVCaptureDeviceInput(device: camera),
              session.canAddInput(input) else {
            return false
        }
        session.addInput(input)
        // Dense symbols need many camera pixels per unit: prefer 4K unless asked otherwise.
        if resolution == .uhd4K, session.canSetSessionPreset(.hd4K3840x2160) {
            session.sessionPreset = .hd4K3840x2160
        } else if session.canSetSessionPreset(.hd1920x1080) {
            session.sessionPreset = .hd1920x1080
        } else {
            session.sessionPreset = .high
        }
        // Frames stay in the sensor's native orientation (the connection is
        // not rotated), so normalized coordinates are capture-device coordinates.
        output.videoSettings = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
        ]
        output.alwaysDiscardsLateVideoFrames = true
        output.setSampleBufferDelegate(self, queue: frameQueue)
        guard session.canAddOutput(output) else { return false }
        session.addOutput(output)
        // No video stabilization: it delays frames and crops them, and a
        // scanner needs the newest, widest frame.
        if let connection = output.connection(with: .video), connection.isVideoStabilizationSupported {
            connection.preferredVideoStabilizationMode = .off
        }
        return true
    }

    /// The three device flags, gated by the master switch.
    private func updateControlFlags() {
        guard let control = cameraControl else { return }
        control.focusOnCode = controlsCamera && focusOnCode
        control.exposureControl = controlsCamera && exposureControl
        control.realDetailZoom = controlsCamera && realDetailZoom
    }

    // MARK: Screen awake

    private func setScreenAwake(_ awake: Bool) {
        if !Thread.isMainThread { DispatchQueue.main.async { self.setScreenAwake(awake) }; return }
        guard awake != heldScreenAwake else { return }
        heldScreenAwake = awake
        CKeyCameraScanner.setIdleTimerDisabled(awake)
    }

    /// Sets `UIApplication.shared.isIdleTimerDisabled` on the main thread.
    /// The application object is reached by name and the property by key, so
    /// this file also compiles for (and does nothing in) app extensions,
    /// which have no `UIApplication`.
    private static func setIdleTimerDisabled(_ disabled: Bool) {
        #if os(iOS)
        let apply = {
            guard !Bundle.main.bundlePath.hasSuffix(".appex") else { return }
            let selector = NSSelectorFromString("sharedApplication")
            guard UIApplication.responds(to: selector),
                  let application = UIApplication.perform(selector)?.takeUnretainedValue() as? UIApplication else {
                return
            }
            application.setValue(disabled, forKey: "idleTimerDisabled")
        }
        if Thread.isMainThread { apply() } else { DispatchQueue.main.async(execute: apply) }
        #endif
    }

    // MARK: AVCaptureVideoDataOutputSampleBufferDelegate

    /// Hands each frame to the tracker and the decoder and returns at once;
    /// the work happens on the processor's queues.
    open func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                            from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        processor.process(pixelBuffer: pixelBuffer,
                          captureTime: CKeyCameraScanner.captureTime(of: sampleBuffer, session: session))
    }
}

/// Converts between capture-device coordinates (the normalized sensor
/// coordinates of `CKeyTrack.corners`, `scanRegion` and AVFoundation's points
/// of interest) and a preview layer's own coordinates, accounting for
/// aspect-fill cropping, rotation and mirroring. It holds the layer weakly;
/// `CKeyCameraPreview(session:geometry:)` fills it in, or set `layer` to your
/// own `AVCaptureVideoPreviewLayer`. Use it on the main thread.
///
/// A custom overlay laid exactly over the preview draws a track with:
///
///     scanner.onTrack = { track in
///         let points = track.flatMap { geometry.viewPoints(fromSensorPoints: $0.corners) }
///         myOverlay.show(points)          // nil: nothing to draw
///     }
public final class CKeyPreviewGeometry: ObservableObject {
    /// The preview layer to convert with.
    public weak var layer: AVCaptureVideoPreviewLayer?

    public init(layer: AVCaptureVideoPreviewLayer? = nil) {
        self.layer = layer
    }

    /// Sensor points in the layer's coordinates; `nil` until the layer shows a camera.
    public func viewPoints(fromSensorPoints points: [CGPoint]) -> [CGPoint]? {
        guard let layer = layer else { return nil }
        return CKeyPreviewGeometry.viewPoints(fromSensorPoints: points, in: layer)
    }

    /// A sensor rectangle's bounding box in the layer's coordinates.
    public func viewRect(fromSensorRect rect: CGRect) -> CGRect? {
        guard let layer = layer else { return nil }
        return CKeyPreviewGeometry.viewRect(fromSensorRect: rect, in: layer)
    }

    /// A rectangle of the layer (for example the reticle) in sensor
    /// coordinates: the value for `CKeyCameraScanner.scanRegion`.
    public func sensorRect(fromViewRect rect: CGRect) -> CGRect? {
        guard let layer = layer else { return nil }
        return CKeyPreviewGeometry.sensorRect(fromViewRect: rect, in: layer)
    }

    /// Normalized sensor points (`CKeyTrack.corners`) in `layer`'s
    /// coordinates, by `layerPointConverted(fromCaptureDevicePoint:)`; `nil`
    /// until the layer is connected to a camera.
    public static func viewPoints(fromSensorPoints points: [CGPoint],
                                  in layer: AVCaptureVideoPreviewLayer) -> [CGPoint]? {
        guard layer.connection != nil else { return nil }
        return points.map { layer.layerPointConverted(fromCaptureDevicePoint: $0) }
    }

    /// A normalized sensor rectangle's bounding box in `layer`'s coordinates.
    public static func viewRect(fromSensorRect rect: CGRect, in layer: AVCaptureVideoPreviewLayer) -> CGRect? {
        guard layer.connection != nil else { return nil }
        let converted = layer.layerRectConverted(fromMetadataOutputRect: rect)
        return converted.isNull || converted.isEmpty ? nil : converted
    }

    /// A rectangle of `layer` (for example the reticle) in normalized sensor
    /// coordinates.
    public static func sensorRect(fromViewRect rect: CGRect, in layer: AVCaptureVideoPreviewLayer) -> CGRect? {
        guard layer.connection != nil, !rect.isNull, !rect.isEmpty else { return nil }
        let converted = layer.metadataOutputRectConverted(fromLayerRect: rect)
        return converted.width > 0 && converted.height > 0 ? converted : nil
    }
}

#if canImport(SwiftUI)
extension CKeyCameraPreview {
    /// Normalized sensor points (`CKeyTrack.corners`) in `layer`'s
    /// coordinates, by `layerPointConverted(fromCaptureDevicePoint:)`; `nil`
    /// until the layer is connected to a camera.
    public static func viewPoints(fromSensorPoints points: [CGPoint],
                                  in layer: AVCaptureVideoPreviewLayer) -> [CGPoint]? {
        CKeyPreviewGeometry.viewPoints(fromSensorPoints: points, in: layer)
    }

    /// A normalized sensor rectangle's bounding box in `layer`'s coordinates.
    public static func viewRect(fromSensorRect rect: CGRect, in layer: AVCaptureVideoPreviewLayer) -> CGRect? {
        CKeyPreviewGeometry.viewRect(fromSensorRect: rect, in: layer)
    }

    /// A rectangle of `layer` (for example the reticle) in normalized sensor
    /// coordinates: the value for `CKeyCameraScanner.scanRegion`.
    public static func sensorRect(fromViewRect rect: CGRect, in layer: AVCaptureVideoPreviewLayer) -> CGRect? {
        CKeyPreviewGeometry.sensorRect(fromViewRect: rect, in: layer)
    }
}

#if os(iOS)
/// A SwiftUI camera preview for a `CKeyCameraScanner`'s session. It keeps the
/// image upright in every orientation and posture (portrait, landscape, a
/// foldable opened either way) with `AVCaptureDevice.RotationCoordinator` on
/// iOS 17 and later; decoding does not depend on it, since CKey symbols read
/// at any rotation. Its static functions convert between sensor and view
/// coordinates for custom overlays.
public struct CKeyCameraPreview: UIViewRepresentable {
    public let session: AVCaptureSession
    /// Receives the preview layer, for `CKeyBracketOverlayView` or a custom overlay.
    public let geometry: CKeyPreviewGeometry?
    /// The reticle in this view's coordinates; `onRegionOfInterest` receives
    /// it in sensor coordinates (the value for `CKeyCameraScanner.scanRegion`)
    /// whenever layout or rotation changes it.
    public let interestRect: CGRect
    public let onRegionOfInterest: ((CGRect) -> Void)?

    public init(session: AVCaptureSession, geometry: CKeyPreviewGeometry? = nil,
                interestRect: CGRect = .null, onRegionOfInterest: ((CGRect) -> Void)? = nil) {
        self.session = session
        self.geometry = geometry
        self.interestRect = interestRect
        self.onRegionOfInterest = onRegionOfInterest
    }

    public func makeUIView(context: Context) -> CKeyPreviewView {
        let view = CKeyPreviewView()
        view.attach(session)
        view.setInterest(interestRect, report: onRegionOfInterest)
        geometry?.layer = view.previewLayer
        return view
    }

    public func updateUIView(_ view: CKeyPreviewView, context: Context) {
        view.attach(session)
        view.setInterest(interestRect, report: onRegionOfInterest)
        geometry?.layer = view.previewLayer
    }
}

/// A UIView backed by an `AVCaptureVideoPreviewLayer`, kept upright with
/// `AVCaptureDevice.RotationCoordinator` (iOS 17 and later) once `attach(_:)`
/// connects it to a session.
public final class CKeyPreviewView: UIView {
    public override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
    public var previewLayer: AVCaptureVideoPreviewLayer {
        // layerClass guarantees the type.
        layer as! AVCaptureVideoPreviewLayer // swiftlint:disable:this force_cast
    }

    private var coordinator: AnyObject?   // AVCaptureDevice.RotationCoordinator on iOS 17+
    private var observation: NSKeyValueObservation?
    private var startObserver: NSObjectProtocol?
    private var interestRect: CGRect = .null
    private var reportInterest: ((CGRect) -> Void)?

    deinit {
        if let startObserver { NotificationCenter.default.removeObserver(startObserver) }
    }

    /// Shows `session` (aspect fill) and keeps it upright. The camera input is
    /// usually added when scanning starts, so rotation tracking begins then.
    public func attach(_ session: AVCaptureSession) {
        if previewLayer.session !== session {
            previewLayer.session = session
            coordinator = nil
            observation = nil
            if let startObserver { NotificationCenter.default.removeObserver(startObserver) }
            startObserver = NotificationCenter.default.addObserver(
                forName: AVCaptureSession.didStartRunningNotification, object: session, queue: .main
            ) { [weak self] _ in self?.makeCoordinator() }
        }
        previewLayer.videoGravity = .resizeAspectFill
        previewLayer.masksToBounds = true
        makeCoordinator()
    }

    /// The reticle in this view's coordinates, reported in sensor coordinates
    /// to `report` now and whenever layout or rotation changes it.
    public func setInterest(_ rect: CGRect, report: ((CGRect) -> Void)?) {
        interestRect = rect
        reportInterest = report
        publishInterest()
    }

    private func publishInterest() {
        guard let reportInterest, !interestRect.isNull, !interestRect.isEmpty, bounds.width > 0, bounds.height > 0,
              let converted = sensorRect(fromViewRect: interestRect) else { return }
        reportInterest(converted)
    }

    private func makeCoordinator() {
        guard #available(iOS 17.0, *) else { publishInterest(); return }
        guard coordinator == nil, let session = previewLayer.session,
              let device = session.inputs.compactMap({ ($0 as? AVCaptureDeviceInput)?.device })
                .first(where: { $0.hasMediaType(.video) }) else {
            applyCurrentAngle()
            return
        }
        let rotation = AVCaptureDevice.RotationCoordinator(device: device, previewLayer: previewLayer)
        coordinator = rotation
        observation = rotation.observe(\.videoRotationAngleForHorizonLevelPreview, options: [.new]) { [weak self] _, _ in
            DispatchQueue.main.async { self?.applyCurrentAngle() }
        }
        applyCurrentAngle()
    }

    private func applyCurrentAngle() {
        if #available(iOS 17.0, *), let rotation = coordinator as? AVCaptureDevice.RotationCoordinator,
           let connection = previewLayer.connection {
            let angle = rotation.videoRotationAngleForHorizonLevelPreview
            if connection.isVideoRotationAngleSupported(angle), connection.videoRotationAngle != angle {
                connection.videoRotationAngle = angle
            }
        }
        publishInterest()
    }

    public override func layoutSubviews() {
        super.layoutSubviews()
        if coordinator == nil { makeCoordinator() } else { applyCurrentAngle() }
    }

    /// Normalized sensor points (`CKeyTrack.corners`) in this view's
    /// coordinates; `nil` until the preview shows a camera.
    public func viewPoints(fromSensorPoints points: [CGPoint]) -> [CGPoint]? {
        CKeyCameraPreview.viewPoints(fromSensorPoints: points, in: previewLayer)
    }

    /// A normalized sensor rectangle's bounding box in this view's coordinates.
    public func viewRect(fromSensorRect rect: CGRect) -> CGRect? {
        CKeyCameraPreview.viewRect(fromSensorRect: rect, in: previewLayer)
    }

    /// A rectangle of this view (for example the reticle) in normalized
    /// sensor coordinates: the value for `CKeyCameraScanner.scanRegion`.
    public func sensorRect(fromViewRect rect: CGRect) -> CGRect? {
        CKeyCameraPreview.sensorRect(fromViewRect: rect, in: previewLayer)
    }
}
#elseif os(macOS)
/// A SwiftUI camera preview for a `CKeyCameraScanner`'s session. Its static
/// functions convert between sensor and view coordinates for custom overlays.
public struct CKeyCameraPreview: NSViewRepresentable {
    public let session: AVCaptureSession
    /// Receives the preview layer, for a custom overlay.
    public let geometry: CKeyPreviewGeometry?

    public init(session: AVCaptureSession, geometry: CKeyPreviewGeometry? = nil) {
        self.session = session
        self.geometry = geometry
    }

    public func makeNSView(context: Context) -> NSView {
        let view = NSView()
        let layer = AVCaptureVideoPreviewLayer(session: session)
        layer.videoGravity = .resizeAspectFill
        view.layer = layer
        view.wantsLayer = true
        geometry?.layer = layer
        return view
    }

    public func updateNSView(_ view: NSView, context: Context) {
        if let layer = view.layer as? AVCaptureVideoPreviewLayer {
            if layer.session !== session { layer.session = session }
            geometry?.layer = layer
        }
    }
}
#endif
#endif
#endif
