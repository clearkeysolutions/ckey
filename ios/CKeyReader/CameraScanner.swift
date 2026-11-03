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
    @Published private(set) var candidateSizes = ""
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
    private let symbolSquareRequest: VNDetectRectanglesRequest = {
        let request = VNDetectRectanglesRequest()
        // A distant CKey is often smaller than the surrounding monitor or
        // laptop. Ask Vision for small, nearly square candidates separately.
        request.maximumObservations = 64
        request.minimumConfidence = 0.05
        request.minimumSize = 0.01
        request.minimumAspectRatio = 0.82
        request.maximumAspectRatio = 1.22
        request.quadratureTolerance = 35
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
            // A CKey's continuous outer border remains detectable after the
            // small payload modules have blurred together at distance. Locate
            // that high-contrast connected component first. This deliberately
            // does not depend on Vision deciding that a rounded code border is
            // a document-shaped rectangle.
            let borderQuads = brightBorderQuads(gray, width: width, height: height, stride: stride)
            for quad in borderQuads.prefix(12) {
                var payload = [UInt8](repeating: 0, count: Int(CKEY_LOCATOR_PAYLOAD_BYTES))
                var diagnostic = ckey_diagnostics()
                let decodeStarted = CACurrentMediaTime()
                var result = CKEY_UNCORRECTABLE
                // A component bounds the illuminated edge; tolerate raster
                // bloom and the one-pixel sampling uncertainty on either side.
                for expansion in [0.94, 0.97, 1.0, 1.03, 1.06] {
                    let adjusted = expandedCKeyQuad(quadPoints(quad), expansion: expansion,
                                                    offsetX: 0, offsetY: 0)
                    result = payload.withUnsafeMutableBufferPointer { payloadBuffer in
                        adjusted.withUnsafeBufferPointer { quadBuffer in
                            ckey_decode_gray8(gray, width, height, stride, quadBuffer.baseAddress,
                                              payloadBuffer.baseAddress, payloadBuffer.count, &diagnostic)
                        }
                    }
                    if result == CKEY_OK { break }
                }
                let decodeElapsed = (CACurrentMediaTime() - decodeStarted) * 1000
                DispatchQueue.main.async { self.decodeMilliseconds = decodeElapsed }
                if result == CKEY_OK {
                    publishDecoded(payload, detectionMilliseconds: (CACurrentMediaTime() - started) * 1000,
                                   candidates: borderQuads.count)
                    return
                }
            }

            let handler = VNImageRequestHandler(cvPixelBuffer: pixelBuffer, orientation: .up, options: [:])
            try handler.perform([symbolSquareRequest, rectangleRequest])
            let squareObservations = symbolSquareRequest.results ?? []
            let generalObservations = rectangleRequest.results ?? []
            let observations = (squareObservations + generalObservations).sorted {
                candidatePriority($0, frameWidth: width, frameHeight: height) <
                candidatePriority($1, frameWidth: width, frameHeight: height)
            }
            let sizeSummary = observations.prefix(3).map { observation in
                let box = observation.boundingBox
                return "\(Int(box.width * CGFloat(width)))×\(Int(box.height * CGFloat(height)))"
            }.joined(separator: ", ")
            DispatchQueue.main.async {
                self.candidateSizes = borderQuads.isEmpty ? sizeSummary : "border \(borderQuads.count); \(sizeSummary)"
            }
            let detectionElapsed = (CACurrentMediaTime() - started) * 1000
            publishProgress(observations.isEmpty ? "Searching for a square…" : "Testing \(observations.count) square candidate(s)…",
                            detectionMilliseconds: detectionElapsed, candidates: observations.count)
            for (candidateIndex, observation) in observations.prefix(24).enumerated() {
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
                if result != CKEY_OK && candidateIndex < 4 {
                    let side = (hypot(points[1].x - points[0].x, points[1].y - points[0].y) +
                                hypot(points[3].x - points[0].x, points[3].y - points[0].y)) * 0.5
                    let shifts = [-0.20, -0.10, 0.0, 0.10, 0.20]
                    refinement: for scale in [0.25, 0.33, 0.40, 0.48, 0.56, 0.64,
                                              0.72, 0.78, 0.86, 0.94, 1.02, 1.10,
                                              1.20, 1.30, 1.40, 1.50, 1.60, 1.80] {
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
                        self.candidateCount = observations.count + borderQuads.count
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

    private func candidatePriority(_ observation: VNRectangleObservation,
                                   frameWidth: Int, frameHeight: Int) -> Double {
        let box = observation.boundingBox
        let aspectError = abs(log(Double(box.width / box.height)))
        let side = sqrt(Double(box.width * box.height * CGFloat(frameWidth * frameHeight)))
        // The square pass finds small codes; the general pass remains useful
        // for close or oblique scans. Prefer geometric squareness first, then
        // avoid taking the full camera frame ahead of a contained symbol.
        let oversizedPenalty = side > Double(min(frameWidth, frameHeight)) * 0.72 ? 2.0 : 0.0
        return aspectError * 10.0 + oversizedPenalty
    }

    private func publishDecoded(_ payload: [UInt8], detectionMilliseconds: Double, candidates: Int) {
        didDecode = true
        let hex = payload.map { String(format: "%02x", $0) }.joined()
        DispatchQueue.main.async {
            self.decodedHex = hex
            self.detectionMilliseconds = detectionMilliseconds
            self.candidateCount = candidates
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

    private func brightBorderQuads(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                                   stride: Int) -> [[Double]] {
        // Work at half resolution: distant locator borders remain several
        // samples thick, while this keeps the connected-component pass small.
        let step = 2
        let sampledWidth = (width + step - 1) / step
        let sampledHeight = (height + step - 1) / step
        var visited = [UInt8](repeating: 0, count: sampledWidth * sampledHeight)
        var queue = [Int]()
        var candidates: [(quad: [Double], score: Double)] = []
        let minFrameSide = min(width, height)

        for sampleY in 0..<sampledHeight {
            for sampleX in 0..<sampledWidth {
                let start = sampleY * sampledWidth + sampleX
                if visited[start] != 0 { continue }
                let sourceX = min(sampleX * step, width - 1)
                let sourceY = min(sampleY * step, height - 1)
                if gray[sourceY * stride + sourceX] < 200 { continue }

                visited[start] = 1
                queue.removeAll(keepingCapacity: true)
                queue.append(start)
                var head = 0
                var minX = sampleX, maxX = sampleX, minY = sampleY, maxY = sampleY
                var count = 0
                while head < queue.count {
                    let node = queue[head]
                    head += 1
                    let x = node % sampledWidth
                    let y = node / sampledWidth
                    minX = min(minX, x); maxX = max(maxX, x)
                    minY = min(minY, y); maxY = max(maxY, y)
                    count += 1
                    for dy in -1...1 {
                        for dx in -1...1 where dx != 0 || dy != 0 {
                            let nx = x + dx, ny = y + dy
                            guard nx >= 0, nx < sampledWidth, ny >= 0, ny < sampledHeight else { continue }
                            let neighbor = ny * sampledWidth + nx
                            if visited[neighbor] != 0 { continue }
                            let px = min(nx * step, width - 1)
                            let py = min(ny * step, height - 1)
                            if gray[py * stride + px] >= 200 {
                                visited[neighbor] = 1
                                queue.append(neighbor)
                            }
                        }
                    }
                }

                let boxWidth = (maxX - minX + 1) * step
                let boxHeight = (maxY - minY + 1) * step
                let side = min(boxWidth, boxHeight)
                guard count >= 24, side >= 40, side < Int(Double(minFrameSide) * 0.78) else { continue }
                let aspect = Double(boxWidth) / Double(boxHeight)
                guard aspect > 0.72, aspect < 1.38 else { continue }
                // Prefer square outlines over large filled bright UI panels.
                let fillRatio = Double(count) / Double((maxX - minX + 1) * (maxY - minY + 1))
                guard fillRatio < 0.38 else { continue }
                let x0 = Double(minX * step), y0 = Double(minY * step)
                let x1 = Double(min((maxX + 1) * step, width - 1))
                let y1 = Double(min((maxY + 1) * step, height - 1))
                let score = abs(log(aspect)) * 10.0 + fillRatio
                candidates.append((quad: [x0, y0, x1, y0, x1, y1, x0, y1], score: score))
            }
        }
        return candidates.sorted { $0.score < $1.score }.map(\.quad)
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
