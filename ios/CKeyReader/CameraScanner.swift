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

        // A CKey's continuous outer border remains detectable after the
        // small payload modules have blurred together at distance. Locate that
        // high-contrast connected component first. This deliberately does not
        // depend on a generic document-rectangle detector.
        // Symbols may be light-on-dark or dark-on-light. Run the same
        // geometric component pass at both polarities; the key fit later
        // resolves the real candidate.
        let borderQuads = borderQuads(gray, width: width, height: height, stride: stride)
        let borderDetectionMilliseconds = (CACurrentMediaTime() - started) * 1000

        // The complete locator is measured from the border component, then
        // optimized using the printed Grid, Ring, Ray, and finder keys. No
        // generic rectangle model or fixed distance ladder participates.
        var liveDetails = borderQuads.isEmpty ? "Border: none" : "Border candidates: \(borderQuads.count)"
        var lastAttemptMilliseconds = 0.0
        for (candidateIndex, quad) in borderQuads.prefix(6).enumerated() {
            let observed = quadPoints(quad)
            // Compact profiles and Profile 3 have deliberately different
            // calibration fields. Never let a strong P1/P2 score select the
            // projective transform used to sample P3 (or vice versa).
            let locatorQuad = refinedCKeyQuad(observed, gray: gray, width: width, height: height,
                                               stride: stride, field: .locator)
            let textQuad = refinedCKeyQuad(observed, gray: gray, width: width, height: height,
                                            stride: stride, field: .text)
            let extendedQuad = refinedCKeyQuad(observed, gray: gray, width: width, height: height,
                                                stride: stride, field: .extended)
            // Profile 0 (scalable family) first: the core aligns its frame edges,
            // identifies the version from its Grid/Ring/Ray Keys, then decodes.
            let scalableQuad = refinedCKeyQuad(observed, gray: gray, width: width, height: height,
                                               stride: stride, field: .scalable)
            let decodeStarted = CACurrentMediaTime()
            if let decoded = decodeScalable(gray: gray, width: width, height: height, stride: stride, quad: scalableQuad) {
                publishDecoded(decoded.bytes, profile: decoded.profile,
                               detectionMilliseconds: borderDetectionMilliseconds,
                               decodeMilliseconds: (CACurrentMediaTime() - decodeStarted) * 1000,
                               candidates: borderQuads.count)
                return
            }
            if let decoded = decodeAnyProfile(gray: gray, width: width, height: height, stride: stride,
                                              locatorQuad: locatorQuad, textQuad: textQuad, extendedQuad: extendedQuad) {
                publishDecoded(decoded.bytes, profile: decoded.profile,
                               detectionMilliseconds: borderDetectionMilliseconds,
                               decodeMilliseconds: (CACurrentMediaTime() - decodeStarted) * 1000,
                               candidates: borderQuads.count)
                return
            }
            lastAttemptMilliseconds = (CACurrentMediaTime() - decodeStarted) * 1000
            let side = measuredSide(observed)
            let p1Fit = calibrationScore(gray, width: width, height: height, stride: stride,
                                         quad: locatorQuad, field: .locator)
            let p2Fit = calibrationScore(gray, width: width, height: height, stride: stride,
                                         quad: textQuad, field: .text)
            let p3Fit = calibrationScore(gray, width: width, height: height, stride: stride,
                                         quad: extendedQuad, field: .extended)
            var p0Version: UInt32 = 0
            let p0Fit = scalableQuad.count == 8 ? scalableQuad.withUnsafeBufferPointer { buffer in
                ckey_calibration_score_scalable_gray8(gray, width, height, stride, buffer.baseAddress, &p0Version)
            } : 0
            // Keep the leading candidate visible in real time. It is the one
            // the decoder is most likely to accept and makes a bad border fit
            // obvious without waiting for a successful scan.
            if candidateIndex == 0 {
                liveDetails = "Border \(String(format: "%.0f", side)) px · fit P0 v\(p0Version) \(String(format: "%.1f", p0Fit)) · P1 \(String(format: "%.1f", p1Fit)) · P2 \(String(format: "%.1f", p2Fit)) · P3 \(String(format: "%.1f", p3Fit))\nAttempt \(String(format: "%.1f", lastAttemptMilliseconds)) ms · \(lastDecodeDiagnostic)"
            }
        }
        DispatchQueue.main.async { self.candidateSizes = borderQuads.isEmpty ? "" : "\(borderQuads.count) measured border component(s)" }
        publishProgress(borderQuads.isEmpty ? "Searching for CKey border and finder keys…" : "Calibrating CKey keys · \(lastDecodeDiagnostic)",
                        detectionMilliseconds: borderDetectionMilliseconds, candidates: borderQuads.count,
                        attemptMilliseconds: lastAttemptMilliseconds, details: liveDetails)
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

    private func measuredSide(_ quad: [CGPoint]) -> Double {
        guard quad.count == 4 else { return 0 }
        return Double((hypot(quad[1].x - quad[0].x, quad[1].y - quad[0].y) +
                       hypot(quad[3].x - quad[0].x, quad[3].y - quad[0].y)) * 0.5)
    }

    private enum CalibrationField { case scalable, locator, text, extended }

    // Fit a border to one profile's canonical field against that profile's
    // fixed keys. Shared C++ core (ckey_refine_quad_gray8): coordinate descent
    // over scale, rotation, and offset, then per-corner projective moves.
    private func refinedCKeyQuad(_ observed: [CGPoint], gray: UnsafePointer<UInt8>,
                                 width: Int, height: Int, stride: Int,
                                 field: CalibrationField) -> [Double] {
        guard observed.count == 4 else { return [] }
        let border = observed.flatMap { [Double($0.x), Double($0.y)] }
        let coreField: ckey_field
        switch field {
        case .scalable: coreField = CKEY_FIELD_SCALABLE
        case .locator: coreField = CKEY_FIELD_LOCATOR
        case .text: coreField = CKEY_FIELD_TEXT
        case .extended: coreField = CKEY_FIELD_EXTENDED
        }
        var refined = [Double](repeating: 0, count: 8)
        let score = border.withUnsafeBufferPointer { input in
            refined.withUnsafeMutableBufferPointer { output in
                ckey_refine_quad_gray8(gray, width, height, stride, input.baseAddress, coreField, output.baseAddress)
            }
        }
        return score >= 0 ? refined : []
    }

    private func calibrationScore(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                                  stride: Int, quad: [Double], field: CalibrationField) -> Double {
        quad.withUnsafeBufferPointer { buffer in
            switch field {
            case .scalable:
                return ckey_calibration_score_scalable_gray8(gray, width, height, stride, buffer.baseAddress, nil)
            case .locator:
                return ckey_calibration_score_gray8(gray, width, height, stride, buffer.baseAddress)
            case .text:
                return ckey_calibration_score_text_gray8(gray, width, height, stride, buffer.baseAddress)
            case .extended:
                return ckey_calibration_score_extended_gray8(gray, width, height, stride, buffer.baseAddress)
            }
        }
    }

    // Profile 0 payloads reach 4,619 bytes (v31 at level M), beyond the
    // 2,048-byte Profile 3 maximum, so this uses its own 64 KB buffer.
    private func decodeScalable(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                                quad: [Double]) -> (bytes: [UInt8], profile: String)? {
        guard quad.count == 8 else { return nil }
        var output = [UInt8](repeating: 0, count: 65535)
        var length = 0
        var version: UInt32 = 0
        var diagnostic = ckey_diagnostics()
        let status = output.withUnsafeMutableBufferPointer { decoded in
            quad.withUnsafeBufferPointer { q in
                ckey_decode_scalable_gray8(gray, width, height, stride, q.baseAddress,
                                           decoded.baseAddress, decoded.count, &length, &version, &diagnostic)
            }
        }
        guard status == CKEY_OK else { return nil }
        return (Array(output.prefix(length)), "Profile 0 · v\(version)")
    }

    private func decodeAnyProfile(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                                  locatorQuad: [Double], textQuad: [Double], extendedQuad: [Double]) -> (bytes: [UInt8], profile: String)? {
        guard locatorQuad.count == 8, textQuad.count == 8, extendedQuad.count == 8 else { return nil }
        var locator = [UInt8](repeating: 0, count: Int(CKEY_LOCATOR_PAYLOAD_BYTES))
        var diagnostic = ckey_diagnostics()
        let locatorStatus = locator.withUnsafeMutableBufferPointer { output in
            locatorQuad.withUnsafeBufferPointer { q in
                ckey_decode_gray8(gray, width, height, stride, q.baseAddress, output.baseAddress, output.count, &diagnostic)
            }
        }
        if locatorStatus == CKEY_OK { return (locator, "Profile 1 · Locator") }
        let locatorDiagnostic = diagnosticText(diagnostic)

        // Dense profiles share the actual native sampler and only succeed after
        // their independent RS and CRC validation. They are intentionally tried
        // after the compact profile so ordinary locator reads retain their fast path.
        var dense = [UInt8](repeating: 0, count: Int(CKEY_EXTENDED_PAYLOAD_MAX_BYTES))
        var length = 0
        let textStatus = dense.withUnsafeMutableBufferPointer { output in
            textQuad.withUnsafeBufferPointer { q in
                ckey_decode_text_gray8(gray, width, height, stride, q.baseAddress,
                                       output.baseAddress, output.count, &length, &diagnostic)
            }
        }
        if textStatus == CKEY_OK { return (Array(dense.prefix(length)), "Profile 2 · Text") }
        let textDiagnostic = diagnosticText(diagnostic)

        length = 0
        let extendedStatus = dense.withUnsafeMutableBufferPointer { output in
            extendedQuad.withUnsafeBufferPointer { q in
                ckey_decode_extended_gray8(gray, width, height, stride, q.baseAddress,
                                           output.baseAddress, output.count, &length, &diagnostic)
            }
        }
        if extendedStatus == CKEY_OK { return (Array(dense.prefix(length)), "Profile 3 · Extended") }
        lastDecodeDiagnostic = "P1 \(locatorDiagnostic) · P2 \(textDiagnostic) · P3 \(diagnosticText(diagnostic))"
        return nil
    }

    private func diagnosticText(_ diagnostic: ckey_diagnostics) -> String {
        withUnsafeBytes(of: diagnostic.message) { bytes in
            guard let address = bytes.baseAddress else { return "decoder failed" }
            return String(cString: address.assumingMemoryBound(to: CChar.self))
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

    private func quadPoints(_ quad: [Double]) -> [CGPoint] {
        guard quad.count == 8 else { return [] }
        return stride(from: 0, to: 8, by: 2).map {
            CGPoint(x: CGFloat(quad[$0]), y: CGFloat(quad[$0 + 1]))
        }
    }

    // Candidate border outlines (both polarities), most border-like first,
    // from the shared C++ core (ckey_find_borders_gray8). Orientation comes
    // from the border's minimum-area rectangle and each side is line-fitted,
    // so rotated and keystoned symbols start from the right geometry.
    private func borderQuads(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                             stride: Int) -> [[Double]] {
        let maxQuads = 6
        var buffer = [Double](repeating: 0, count: maxQuads * 8)
        let count = buffer.withUnsafeMutableBufferPointer { output in
            ckey_find_borders_gray8(gray, width, height, stride, output.baseAddress, maxQuads)
        }
        return (0..<count).map { Array(buffer[($0 * 8)..<($0 * 8 + 8)]) }
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
