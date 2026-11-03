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
    @Published private(set) var decodedProfile = ""
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
        // CKey's rounded border and inset arc finders are less box-like than
        // a QR finder. Structural calibration below resolves candidates.
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
            self.decodedProfile = ""
            self.status = "Looking for a CKey…"
        }
    }

    private func configureAndRun() {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            if !self.hasConfigured {
                self.session.beginConfiguration()
                // A distant CKey must retain several camera pixels per module.
                // `.high` may negotiate a smaller stream on some devices.
                self.session.sessionPreset = self.session.canSetSessionPreset(.hd1920x1080) ? .hd1920x1080 : .high
                guard let camera = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back),
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
                    if camera.isExposureModeSupported(.continuousAutoExposure) {
                        camera.exposureMode = .continuousAutoExposure
                    }
                    camera.isSubjectAreaChangeMonitoringEnabled = true
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
                let decodeStarted = CACurrentMediaTime()
                let adjusted = refinedCKeyQuad(quadPoints(quad), gray: gray, width: width,
                                                height: height, stride: stride)
                if let decoded = decodeAnyProfile(gray: gray, width: width, height: height, stride: stride,
                                                  locatorQuad: adjusted, denseQuad: quad) {
                    let decodeElapsed = (CACurrentMediaTime() - decodeStarted) * 1000
                    DispatchQueue.main.async { self.decodeMilliseconds = decodeElapsed }
                    publishDecoded(decoded.bytes, profile: decoded.profile, detectionMilliseconds: (CACurrentMediaTime() - started) * 1000,
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
            for observation in observations.prefix(24) {
                // Vision points use normalized lower-left coordinates. CKey images use top-left pixels.
                let points = [observation.topLeft, observation.topRight, observation.bottomRight, observation.bottomLeft]
                    .map { CGPoint(x: $0.x * CGFloat(width), y: (1 - $0.y) * CGFloat(height)) }
                let decodeStarted = CACurrentMediaTime()
                let refined = refinedCKeyQuad(points, gray: gray, width: width, height: height, stride: stride)
                let denseQuad = expandedCKeyQuad(points, expansion: 1.0, offsetX: 0, offsetY: 0)
                if let decoded = decodeAnyProfile(gray: gray, width: width, height: height, stride: stride,
                                                  locatorQuad: refined, denseQuad: denseQuad) {
                    let decodeElapsed = (CACurrentMediaTime() - decodeStarted) * 1000
                    didDecode = true
                    DispatchQueue.main.async {
                        self.decodedHex = decoded.bytes.map { String(format: "%02x", $0) }.joined()
                        self.decodedProfile = decoded.profile
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

    private func refinedCKeyQuad(_ observed: [CGPoint], gray: UnsafePointer<UInt8>,
                                 width: Int, height: Int, stride: Int) -> [Double] {
        guard observed.count == 4 else { return [] }
        let side = (hypot(observed[1].x - observed[0].x, observed[1].y - observed[0].y) +
                    hypot(observed[3].x - observed[0].x, observed[3].y - observed[0].y)) * 0.5
        // Geometry is estimated continuously from the three permanent keys,
        // not selected from a table of symbol sizes. Coordinate descent starts
        // at the detector's measured quad and halves its physical step each
        // round, converging to the best calibrated field at any pixel scale.
        var scale = 1.0
        var offsetX = 0.0
        var offsetY = 0.0
        var scaleStep = 0.14
        var offsetStep = Double(side) * 0.08
        var best = expandedCKeyQuad(observed, expansion: scale, offsetX: offsetX, offsetY: offsetY)
        var bestScore = calibrationScore(gray, width: width, height: height, stride: stride, quad: best)
        let directions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0),
                          (0.0, 1.0, 0.0), (0.0, -1.0, 0.0),
                          (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)]
        for _ in 0..<4 {
            var roundScale = scale
            var roundOffsetX = offsetX
            var roundOffsetY = offsetY
            var roundScore = bestScore
            var roundQuad = best
            for direction in directions {
                let candidateScale = max(0.35, scale + direction.0 * scaleStep)
                let candidateOffsetX = offsetX + direction.1 * offsetStep
                let candidateOffsetY = offsetY + direction.2 * offsetStep
                let candidate = expandedCKeyQuad(observed, expansion: candidateScale,
                                                  offsetX: candidateOffsetX, offsetY: candidateOffsetY)
                let score = calibrationScore(gray, width: width, height: height, stride: stride, quad: candidate)
                if score > roundScore {
                    roundScore = score
                    roundScale = candidateScale
                    roundOffsetX = candidateOffsetX
                    roundOffsetY = candidateOffsetY
                    roundQuad = candidate
                }
            }
            scale = roundScale
            offsetX = roundOffsetX
            offsetY = roundOffsetY
            bestScore = roundScore
            best = roundQuad
            scaleStep *= 0.5
            offsetStep *= 0.5
        }
        return best
    }

    private func calibrationScore(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                                  stride: Int, quad: [Double]) -> Double {
        quad.withUnsafeBufferPointer { buffer in
            ckey_calibration_score_gray8(gray, width, height, stride, buffer.baseAddress)
        }
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

    private func decodeAnyProfile(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                                  locatorQuad: [Double], denseQuad: [Double]) -> (bytes: [UInt8], profile: String)? {
        guard locatorQuad.count == 8, denseQuad.count == 8 else { return nil }
        var locator = [UInt8](repeating: 0, count: Int(CKEY_LOCATOR_PAYLOAD_BYTES))
        var diagnostic = ckey_diagnostics()
        let locatorStatus = locator.withUnsafeMutableBufferPointer { output in
            locatorQuad.withUnsafeBufferPointer { q in
                ckey_decode_gray8(gray, width, height, stride, q.baseAddress, output.baseAddress, output.count, &diagnostic)
            }
        }
        if locatorStatus == CKEY_OK { return (locator, "Profile 1 · Locator") }

        // Dense profiles share the actual native sampler and only succeed after
        // their independent RS and CRC validation. They are intentionally tried
        // after the compact profile so ordinary locator reads retain their fast path.
        var dense = [UInt8](repeating: 0, count: Int(CKEY_EXTENDED_PAYLOAD_MAX_BYTES))
        var length = 0
        let textStatus = dense.withUnsafeMutableBufferPointer { output in
            denseQuad.withUnsafeBufferPointer { q in
                ckey_decode_text_gray8(gray, width, height, stride, q.baseAddress,
                                       output.baseAddress, output.count, &length, &diagnostic)
            }
        }
        if textStatus == CKEY_OK { return (Array(dense.prefix(length)), "Profile 2 · Text") }

        length = 0
        let extendedStatus = dense.withUnsafeMutableBufferPointer { output in
            denseQuad.withUnsafeBufferPointer { q in
                ckey_decode_extended_gray8(gray, width, height, stride, q.baseAddress,
                                           output.baseAddress, output.count, &length, &diagnostic)
            }
        }
        if extendedStatus == CKEY_OK { return (Array(dense.prefix(length)), "Profile 3 · Extended") }
        return nil
    }

    private func publishDecoded(_ payload: [UInt8], profile: String, detectionMilliseconds: Double, candidates: Int) {
        didDecode = true
        let hex = payload.map { String(format: "%02x", $0) }.joined()
        DispatchQueue.main.async {
            self.decodedHex = hex
            self.decodedProfile = profile
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
                // Allow a blurred or dim display border to remain a candidate;
                // the calibration keys, RS correction, and CRC are the false-
                // positive gate rather than this coarse luminance threshold.
                if gray[sourceY * stride + sourceX] < 180 { continue }

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
                            if gray[py * stride + px] >= 180 {
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
