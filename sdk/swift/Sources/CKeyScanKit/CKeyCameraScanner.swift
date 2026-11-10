#if canImport(AVFoundation) && (os(iOS) || os(macOS))
import AVFoundation
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

/// A continuous AVFoundation camera scanner. It processes live frames on a
/// background queue (at most `maxFramesPerSecond`), never needs a still
/// capture, and emits the first valid decode of a session through
/// `onResult` on the main queue. Frames are then ignored until `scanAgain()`.
///
/// Add `NSCameraUsageDescription` to the app's Info.plist. On macOS, sandboxed
/// apps also need the camera entitlement.
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
    /// Frame-rate cap for decoding; stale frames are discarded.
    public var maxFramesPerSecond: Double = 20

    private let observer: CKeyScanObserver?
    private let sessionQueue = DispatchQueue(label: "org.openckey.scanner.session")
    private let frameQueue = DispatchQueue(label: "org.openckey.scanner.frames", qos: .userInitiated)
    private let output = AVCaptureVideoDataOutput()
    private var configured = false
    // Frame-queue state.
    private var didDecode = false
    private var lastFrameTime: CFTimeInterval = 0
    // Main-thread state.
    private var sessionOpen = false

    /// Capture resolution. `.uhd4K` reads the densest symbols; `.hd1080` uses
    /// far less memory (camera extensions, older devices).
    public enum Resolution: Sendable { case uhd4K, hd1080 }
    private let resolution: Resolution

    /// - Parameters:
    ///   - observer: optional session observer (for example your own metrics);
    ///     held strongly. `nil` for none.
    ///   - resolution: preferred capture resolution; falls back to the best
    ///     the device supports.
    public init(observer: CKeyScanObserver?, resolution: Resolution = .uhd4K) {
        self.observer = observer
        self.resolution = resolution
        super.init()
    }

    deinit {
        if sessionOpen { observer?.scanSessionDidEnd() }
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
        let session = self.session
        sessionQueue.async { if session.isRunning { session.stopRunning() } }
    }

    /// Starts a new scan session after a decode, keeping the camera running.
    public func scanAgain() {
        endSession()
        beginSession()
        frameQueue.async { self.didDecode = false }
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
        DispatchQueue.main.async { self.onError?(CKeyError(code, message)) }
    }

    private func run() {
        if !sessionOpen { beginSession() }
        frameQueue.async { self.didDecode = false }
        sessionQueue.async { [weak self] in
            guard let self else { return }
            if !self.configured {
                guard self.configure() else {
                    self.fail("Could not configure the camera.")
                    return
                }
                self.configured = true
            }
            if !self.session.isRunning { self.session.startRunning() }
        }
    }

    private func configure() -> Bool {
        session.beginConfiguration()
        defer { session.commitConfiguration() }
        // Dense symbols need many camera pixels per unit: prefer 4K unless asked otherwise.
        if resolution == .uhd4K, session.canSetSessionPreset(.hd4K3840x2160) {
            session.sessionPreset = .hd4K3840x2160
        } else if session.canSetSessionPreset(.hd1920x1080) {
            session.sessionPreset = .hd1920x1080
        } else {
            session.sessionPreset = .high
        }
        #if os(iOS)
        // The physical wide camera, so iOS cannot swap lenses mid-measurement.
        let camera = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInWideAngleCamera],
                                                      mediaType: .video, position: .back).devices.first
            ?? AVCaptureDevice.default(for: .video)
        #else
        let camera = AVCaptureDevice.default(for: .video)
        #endif
        guard let camera, let input = try? AVCaptureDeviceInput(device: camera), session.canAddInput(input) else {
            return false
        }
        session.addInput(input)
        if (try? camera.lockForConfiguration()) != nil {
            if camera.isFocusModeSupported(.continuousAutoFocus) { camera.focusMode = .continuousAutoFocus }
            if camera.isExposureModeSupported(.continuousAutoExposure) { camera.exposureMode = .continuousAutoExposure }
            #if os(iOS)
            if camera.isAutoFocusRangeRestrictionSupported { camera.autoFocusRangeRestriction = .none }
            camera.videoZoomFactor = 1.0
            camera.isSubjectAreaChangeMonitoringEnabled = false
            #endif
            camera.unlockForConfiguration()
        }
        output.videoSettings = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
        ]
        output.alwaysDiscardsLateVideoFrames = true
        output.setSampleBufferDelegate(self, queue: frameQueue)
        guard session.canAddOutput(output) else { return false }
        session.addOutput(output)
        return true
    }

    // MARK: AVCaptureVideoDataOutputSampleBufferDelegate

    open func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                            from connection: AVCaptureConnection) {
        if didDecode { return }
        let now = CACurrentMediaTime()
        if maxFramesPerSecond > 0, now - lastFrameTime < 1 / maxFramesPerSecond { return }
        lastFrameTime = now
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let result = CKeyDecoder.decode(pixelBuffer: pixelBuffer)
        observer?.scanFrameProcessed(milliseconds: (CACurrentMediaTime() - now) * 1000)
        guard let result else { return }
        didDecode = true
        observer?.scanSessionDidDecode(result)
        DispatchQueue.main.async { self.onResult?(result) }
    }
}

#if canImport(SwiftUI)
#if os(iOS)
/// A SwiftUI camera preview for a `CKeyCameraScanner`'s session.
public struct CKeyCameraPreview: UIViewRepresentable {
    public let session: AVCaptureSession
    public init(session: AVCaptureSession) { self.session = session }

    public func makeUIView(context: Context) -> CKeyPreviewView {
        let view = CKeyPreviewView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspectFill
        return view
    }

    public func updateUIView(_ view: CKeyPreviewView, context: Context) {
        if view.previewLayer.session !== session { view.previewLayer.session = session }
    }
}

/// A UIView backed by an `AVCaptureVideoPreviewLayer`.
public final class CKeyPreviewView: UIView {
    public override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
    public var previewLayer: AVCaptureVideoPreviewLayer {
        // layerClass guarantees the type.
        layer as! AVCaptureVideoPreviewLayer // swiftlint:disable:this force_cast
    }
}
#elseif os(macOS)
/// A SwiftUI camera preview for a `CKeyCameraScanner`'s session.
public struct CKeyCameraPreview: NSViewRepresentable {
    public let session: AVCaptureSession
    public init(session: AVCaptureSession) { self.session = session }

    public func makeNSView(context: Context) -> NSView {
        let view = NSView()
        let layer = AVCaptureVideoPreviewLayer(session: session)
        layer.videoGravity = .resizeAspectFill
        view.layer = layer
        view.wantsLayer = true
        return view
    }

    public func updateNSView(_ view: NSView, context: Context) {
        if let layer = view.layer as? AVCaptureVideoPreviewLayer, layer.session !== session {
            layer.session = session
        }
    }
}
#endif
#endif
#endif
