import AVFoundation
import AudioToolbox
import Foundation
import QuartzCore
import SwiftUI

final class CameraScanner: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    @Published private(set) var status = "Starting camera…"
    @Published private(set) var decodedHex: String?
    /// The payload as text, when it is valid UTF-8 with no control characters
    /// other than tab, LF, and CR; nil for binary payloads (hex only).
    @Published private(set) var decodedText: String?
    /// The tappable action a text payload offers (link, email, call, map...).
    @Published private(set) var decodedAction: PayloadAction?
    @Published private(set) var decodedProfile = ""
    @Published private(set) var decodeMilliseconds = 0.0
    @Published private(set) var detectionMilliseconds = 0.0
    @Published private(set) var candidateCount = 0
    @Published private(set) var candidateSizes = ""
    @Published private(set) var scanDetails = "Waiting for a camera frame."
    @Published private(set) var isRunning = false

    private let sessionQueue = DispatchQueue(label: "solutions.clearkey.ckey-reader.session")
    private let frameQueue = DispatchQueue(label: "solutions.clearkey.ckey-reader.frames", qos: .userInitiated)
    private let output = AVCaptureVideoDataOutput()
    private var lastFrameTime = 0.0
    private var lastProgressTime = 0.0
    private var hasConfigured = false
    private var didDecode = false
    private var lastDecodeDiagnostic = ""
    // Profile 0 payloads reach 4,619 bytes (v31 at level M), beyond the
    // 2,048-byte Profile 3 maximum: one 64 KB buffer, reused on the frame queue.
    private var payloadBuffer = [UInt8](repeating: 0, count: 65535)

    func start() {
        switch AVCaptureDevice.authorizationStatus(for: .video) {
        case .authorized: configureAndRun()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .video) { [weak self] allowed in
                guard let self else { return }
                if allowed { self.configureAndRun() }
                else { DispatchQueue.main.async { self.status = "Camera access is required to scan." } }
            }
        default:
            DispatchQueue.main.async { self.status = "Enable camera access for CKey Reader in Settings." }
        }
    }

    func stop() {
        sessionQueue.async { [weak self] in
            guard let self, self.session.isRunning else { return }
            self.session.stopRunning()
            DispatchQueue.main.async { self.isRunning = false }
        }
    }

    func scanAgain() {
        frameQueue.async { self.didDecode = false }
        DispatchQueue.main.async {
            self.decodedHex = nil
            self.decodedText = nil
            self.decodedAction = nil
            self.decodedProfile = ""
            self.status = "Looking for a CKey…"
        }
    }

    private func configureAndRun() {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            if !self.hasConfigured {
                self.session.beginConfiguration()
                // A CKey must retain enough camera pixels per symbol unit:
                // the core decoders need about 6 (P1), 10 (P2), and 11 (P3)
                // px/unit. Profile 3's 116-unit field therefore needs about
                // 1,280 px across, which a 1080-line frame cannot supply, so
                // prefer 4K. `.high` may negotiate a smaller stream.
                if self.session.canSetSessionPreset(.hd4K3840x2160) {
                    self.session.sessionPreset = .hd4K3840x2160
                } else {
                    self.session.sessionPreset = self.session.canSetSessionPreset(.hd1920x1080) ? .hd1920x1080 : .high
                }
                // Choose the physical wide device explicitly. Selecting a
                // virtual dual/triple device lets iOS swap focal lengths while
                // we are estimating a projective measurement, which invalidates
                // the scale model between adjacent frames.
                let discovery = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInWideAngleCamera], mediaType: .video, position: .back)
                guard let camera = discovery.devices.first,
                      let input = try? AVCaptureDeviceInput(device: camera),
                      self.session.canAddInput(input) else {
                    self.session.commitConfiguration()
                    DispatchQueue.main.async { self.status = "Could not configure the back camera." }
                    return
                }
                self.session.addInput(input)
                do {
                    try camera.lockForConfiguration()
                    if camera.isFocusModeSupported(.continuousAutoFocus) {
                        camera.focusMode = .continuousAutoFocus
                    }
                    if camera.isAutoFocusRangeRestrictionSupported {
                        // The physical wide camera is already fixed at 1×, so
                        // no lens will swap. Do not force its optics to the
                        // far range: a CKey may legitimately be shown at a
                        // close, medium, or distant measured size.
                        camera.autoFocusRangeRestriction = .none
                    }
                    if camera.isExposureModeSupported(.continuousAutoExposure) {
                        camera.exposureMode = .continuousAutoExposure
                    }
                    camera.videoZoomFactor = 1.0
                    camera.isSubjectAreaChangeMonitoringEnabled = false
                    camera.unlockForConfiguration()
                } catch { }
                self.output.videoSettings = [
                    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                ]
                self.output.alwaysDiscardsLateVideoFrames = true
                self.output.setSampleBufferDelegate(self, queue: self.frameQueue)
                guard self.session.canAddOutput(self.output) else {
                    self.session.commitConfiguration()
                    DispatchQueue.main.async { self.status = "Could not configure camera frames." }
                    return
                }
                self.session.addOutput(self.output)
                if let connection = self.output.connection(with: .video),
                   connection.isVideoRotationAngleSupported(90) {
                    connection.videoRotationAngle = 90
                    if connection.isVideoStabilizationSupported { connection.preferredVideoStabilizationMode = .off }
                }
                self.session.commitConfiguration()
                self.hasConfigured = true
            }
            if !self.session.isRunning { self.session.startRunning() }
            DispatchQueue.main.async {
                self.isRunning = true
                self.status = "Looking for a CKey…"
            }
        }
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        if didDecode { return }
        let now = CACurrentMediaTime()
        if now - lastFrameTime < 0.05 { return } // Sample up to 20 frames/s while discarding stale frames.
        lastFrameTime = now
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer),
              CVPixelBufferGetPlaneCount(pixelBuffer) >= 1 else { return }

        let started = CACurrentMediaTime()
        CVPixelBufferLockBaseAddress(pixelBuffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, .readOnly) }
        guard let gray = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0)?.assumingMemoryBound(to: UInt8.self) else { return }
        let width = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0)
        let height = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0)
        let stride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0)

        // One call into the shared core (ckey_scan_frame_gray8): border search
        // at both polarities, then for each candidate Profile 0, 1, 2 and 3,
        // each refined against its own keys and accepted only after
        // Reed-Solomon and CRC validation. Nothing is located or decoded here.
        var options = ckey_scan_options()
        ckey_scan_options_init(&options)
        var scan = ckey_scan_result()
        let capacity = payloadBuffer.count
        let scanStatus = payloadBuffer.withUnsafeMutableBufferPointer { output in
            ckey_scan_frame_gray8(gray, width, height, stride, &options, output.baseAddress, capacity, &scan)
        }
        let scanMilliseconds = (CACurrentMediaTime() - started) * 1000
        let candidates = Int(scan.candidates)
        if scanStatus == CKEY_OK, Int(scan.payload_length) <= capacity {
            publishDecoded(Array(payloadBuffer.prefix(Int(scan.payload_length))), profile: profileLabel(scan),
                           detectionMilliseconds: scanMilliseconds, decodeMilliseconds: scanMilliseconds,
                           candidates: candidates)
            return
        }
        lastDecodeDiagnostic = scanMessage(scan)

        // Keep the leading candidate visible in real time: its measured side
        // and Profile 0 fit make a bad border fit obvious without waiting for
        // a successful scan.
        var liveDetails = candidates == 0 ? "Border: none" : "Border candidates: \(candidates)"
        if scan.located != 0 {
            let q = scan.quad
            let side = (hypot(q.2 - q.0, q.3 - q.1) + hypot(q.6 - q.0, q.7 - q.1)) * 0.5
            let sideText = String(format: "%.0f", side)
            let fitText = String(format: "%.1f", scan.score)
            let attemptText = String(format: "%.1f", scanMilliseconds)
            let cut = (scan.flags & CKEY_SCAN_FLAG_SYMBOL_CUT) != 0
            liveDetails = "Border \(sideText) px · fit P0 \(fitText)"
            if cut { liveDetails += " · cut by the frame edge" }
            liveDetails += "\nAttempt \(attemptText) ms · \(lastDecodeDiagnostic)"
        }
        DispatchQueue.main.async { self.candidateSizes = candidates == 0 ? "" : "\(candidates) measured border component(s)" }
        publishProgress(candidates == 0 ? "Searching for CKey border and finder keys…" : "Calibrating CKey keys · \(lastDecodeDiagnostic)",
                        detectionMilliseconds: scanMilliseconds, candidates: candidates,
                        attemptMilliseconds: scanMilliseconds, details: liveDetails)
    }

    /// The label shown for a decoded symbol, from the scan result.
    private func profileLabel(_ scan: ckey_scan_result) -> String {
        switch scan.profile {
        case CKEY_FIELD_SCALABLE.rawValue:
            switch scan.layout {
            case CKEY_LAYOUT_PRINT.rawValue: return "Profile 0.1 · print · v\(scan.version)"
            case CKEY_LAYOUT_STANDARD_GROUPED.rawValue: return "Profile 0 · grouped · v\(scan.version)"
            case CKEY_LAYOUT_PRINT_GROUPED.rawValue: return "Profile 0.1 · print · grouped · v\(scan.version)"
            default: return "Profile 0 · v\(scan.version)"
            }
        case CKEY_FIELD_LOCATOR.rawValue: return "Profile 1 · Locator"
        case CKEY_FIELD_TEXT.rawValue: return "Profile 2 · Text"
        default: return "Profile 3 · Extended"
        }
    }

    private func scanMessage(_ scan: ckey_scan_result) -> String {
        withUnsafeBytes(of: scan.message) { raw -> String in
            let bytes = raw.bindMemory(to: UInt8.self)
            let end = bytes.firstIndex(of: 0) ?? bytes.count
            return String(decoding: bytes[0..<end], as: UTF8.self)
        }
    }

    private func publishProgress(_ text: String, detectionMilliseconds: Double, candidates: Int,
                                 attemptMilliseconds: Double = 0, details: String = "",
                                 force: Bool = false) {
        let now = CACurrentMediaTime()
        guard force || now - lastProgressTime >= 0.5 else { return }
        lastProgressTime = now
        DispatchQueue.main.async {
            self.status = text
            self.detectionMilliseconds = detectionMilliseconds
            self.decodeMilliseconds = attemptMilliseconds
            self.candidateCount = candidates
            if !details.isEmpty { self.scanDetails = details }
        }
    }

    static func readableText(_ payload: [UInt8]) -> String? {
        guard !payload.isEmpty, let text = String(bytes: payload, encoding: .utf8) else { return nil }
        let printable = text.unicodeScalars.allSatisfy { scalar in
            // Tab, LF, and CR (vCards use CR LF line endings) are allowed.
            scalar == "\n" || scalar == "\r" || scalar == "\t" || !CharacterSet.controlCharacters.contains(scalar)
        }
        return printable ? text : nil
    }

    private func publishDecoded(_ payload: [UInt8], profile: String, detectionMilliseconds: Double,
                                decodeMilliseconds: Double, candidates: Int) {
        didDecode = true
        let hex = payload.map { String(format: "%02x", $0) }.joined()
        let text = CameraScanner.readableText(payload)
        let action = text.flatMap(PayloadAction.interpret)
        DispatchQueue.main.async {
            self.decodedHex = hex
            self.decodedText = text
            self.decodedAction = action
            self.decodedProfile = profile
            self.detectionMilliseconds = detectionMilliseconds
            self.candidateCount = candidates
            self.decodeMilliseconds = decodeMilliseconds
            self.status = "CKey decoded"
            AudioServicesPlaySystemSound(SystemSoundID(1520))
        }
    }
}

struct CameraPreview: UIViewRepresentable {
    let session: AVCaptureSession

    func makeUIView(context: Context) -> PreviewView {
        let view = PreviewView()
        view.previewLayer.session = session
        view.previewLayer.videoGravity = .resizeAspectFill
        if let connection = view.previewLayer.connection,
           connection.isVideoRotationAngleSupported(90) {
            connection.videoRotationAngle = 90
        }
        return view
    }

    func updateUIView(_ uiView: PreviewView, context: Context) {
        if uiView.previewLayer.session !== session { uiView.previewLayer.session = session }
    }
}

final class PreviewView: UIView {
    override class var layerClass: AnyClass { AVCaptureVideoPreviewLayer.self }
    var previewLayer: AVCaptureVideoPreviewLayer { layer as! AVCaptureVideoPreviewLayer }
}
