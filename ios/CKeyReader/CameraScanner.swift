import AVFoundation
import AudioToolbox
import Foundation
import QuartzCore
import SwiftUI
import Vision

final class CameraScanner: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    let session = AVCaptureSession()
    @Published private(set) var status = "Starting camera…"
    @Published private(set) var decodedHex: String?
    @Published private(set) var decodeMilliseconds = 0.0
    @Published private(set) var detectionMilliseconds = 0.0
    @Published private(set) var candidateCount = 0
    @Published private(set) var isRunning = false

    private let sessionQueue = DispatchQueue(label: "solutions.clearkey.ckey-reader.session")
    private let frameQueue = DispatchQueue(label: "solutions.clearkey.ckey-reader.frames", qos: .userInitiated)
    private let output = AVCaptureVideoDataOutput()
    private var lastFrameTime = 0.0
    private var lastProgressTime = 0.0
    private var hasConfigured = false
    private var didDecode = false
    private let rectangleRequest: VNDetectRectanglesRequest = {
        let request = VNDetectRectanglesRequest()
        // CKey's rounded-square outline is less box-like than a QR finder.
        // Keep localization permissive; the key/ECC/CRC checks reject false quads.
        request.maximumObservations = 16
        request.minimumConfidence = 0.2
        request.minimumSize = 0.06
        request.minimumAspectRatio = 0.62
        request.maximumAspectRatio = 1.60
        request.quadratureTolerance = 45
        return request
    }()

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
            self.status = "Looking for a CKey…"
        }
    }

    private func configureAndRun() {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            if !self.hasConfigured {
                self.session.beginConfiguration()
                self.session.sessionPreset = .high
                guard let camera = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
                      let input = try? AVCaptureDeviceInput(device: camera),
                      self.session.canAddInput(input) else {
                    self.session.commitConfiguration()
                    DispatchQueue.main.async { self.status = "Could not configure the back camera." }
                    return
                }
                self.session.addInput(input)
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

        do {
            let handler = VNImageRequestHandler(cvPixelBuffer: pixelBuffer, orientation: .up, options: [:])
            try handler.perform([rectangleRequest])
            let observations = (rectangleRequest.results ?? []).sorted {
                $0.boundingBox.width * $0.boundingBox.height > $1.boundingBox.width * $1.boundingBox.height
            }
            let detectionElapsed = (CACurrentMediaTime() - started) * 1000
            publishProgress(observations.isEmpty ? "Searching for a square…" : "Testing \(observations.count) square candidate(s)…",
                            detectionMilliseconds: detectionElapsed, candidates: observations.count)
            for (candidateIndex, observation) in observations.prefix(8).enumerated() {
                // Vision points use normalized lower-left coordinates. CKey images use top-left pixels.
                let points = [observation.topLeft, observation.topRight, observation.bottomRight, observation.bottomLeft]
                    .map { CGPoint(x: $0.x * CGFloat(width), y: (1 - $0.y) * CGFloat(height)) }
                var payload = [UInt8](repeating: 0, count: Int(CKEY_LOCATOR_PAYLOAD_BYTES))
                var diagnostic = ckey_diagnostics()
                let decodeStarted = CACurrentMediaTime()
                var result = CKEY_UNCORRECTABLE
                // Vision's rectangle corners may follow the border's inner edge,
                // outer edge, or line intersection. Try nearby canonical bounds.
                let fastScales = [1.0, 29.0 / 28.0, 1.06, 0.96]
                for expansion in fastScales {
                    let quad = expandedCKeyQuad(points, expansion: expansion, offsetX: 0, offsetY: 0)
                    result = payload.withUnsafeMutableBufferPointer { payloadBuffer in
                        quad.withUnsafeBufferPointer { quadBuffer in
                            ckey_decode_gray8(gray, width, height, stride, quadBuffer.baseAddress,
                                              payloadBuffer.baseAddress, payloadBuffer.count, &diagnostic)
                        }
                    }
                    if result == CKEY_OK { break }
                }
                // At distance, Vision may select the surrounding preview or
                // editor frame instead of the code. Search a broad set of
                // symbol-sized quads around leading candidates only.
                if result != CKEY_OK && candidateIndex < 6 {
                    let side = (hypot(points[1].x - points[0].x, points[1].y - points[0].y) +
                                hypot(points[3].x - points[0].x, points[3].y - points[0].y)) * 0.5
                    let shifts = [-0.08, 0.0, 0.08]
                    refinement: for scale in [0.40, 0.50, 0.60, 0.70, 0.78, 0.86,
                                              0.94, 1.02, 1.10, 1.20, 1.30, 1.40] {
                        for ox in shifts {
                            for oy in shifts {
                            let quad = expandedCKeyQuad(points, expansion: scale,
                                                        offsetX: ox * side, offsetY: oy * side)
                            result = payload.withUnsafeMutableBufferPointer { payloadBuffer in
                                quad.withUnsafeBufferPointer { quadBuffer in
                                    ckey_decode_gray8(gray, width, height, stride, quadBuffer.baseAddress,
                                                      payloadBuffer.baseAddress, payloadBuffer.count, &diagnostic)
                                }
                            }
                            if result == CKEY_OK { break refinement }
                            }
                        }
                    }
                }
                let decodeElapsed = (CACurrentMediaTime() - decodeStarted) * 1000
                DispatchQueue.main.async { self.decodeMilliseconds = decodeElapsed }
                if result == CKEY_OK {
                    didDecode = true
                    let hex = payload.map { String(format: "%02x", $0) }.joined()
                    DispatchQueue.main.async {
                        self.decodedHex = hex
                        self.detectionMilliseconds = detectionElapsed
                        self.candidateCount = observations.count
                        self.decodeMilliseconds = decodeElapsed
                        self.status = "CKey decoded"
                        AudioServicesPlaySystemSound(SystemSoundID(1520))
                    }
                    return
                }
            }
            if !observations.isEmpty {
                publishProgress("Square found; CKey checks did not match. Hold steady and fill more of the frame.",
                                detectionMilliseconds: detectionElapsed, candidates: observations.count,
                                force: true)
            }
        } catch {
            publishProgress("Camera analysis failed: \(error.localizedDescription)", detectionMilliseconds: 0, candidates: 0)
        }
    }

    private func publishProgress(_ text: String, detectionMilliseconds: Double, candidates: Int,
                                 force: Bool = false) {
        let now = CACurrentMediaTime()
        guard force || now - lastProgressTime >= 0.5 else { return }
        lastProgressTime = now
        DispatchQueue.main.async {
            self.status = text
            self.detectionMilliseconds = detectionMilliseconds
            self.candidateCount = candidates
        }
    }

    private func expandedCKeyQuad(_ observed: [CGPoint], expansion scale: Double,
                                  offsetX: Double, offsetY: Double) -> [Double] {
        // Vision can fit the inner border, outer border, or the SVG canvas.
        // Keep the hypotheses distinct so each candidate can reach the same
        // canonical 29×29 sampling field at a different detected boundary.
        let centerX = observed.map { Double($0.x) }.reduce(0.0, +) / 4.0
        let centerY = observed.map { Double($0.y) }.reduce(0.0, +) / 4.0
        var quad: [Double] = []
        quad.reserveCapacity(observed.count * 2)
        for point in observed {
            let x = Double(point.x)
            let y = Double(point.y)
            quad.append(centerX + (x - centerX) * scale + offsetX)
            quad.append(centerY + (y - centerY) * scale + offsetY)
        }
        return quad
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
