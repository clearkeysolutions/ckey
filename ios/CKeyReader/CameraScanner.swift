import AVFoundation
import AudioToolbox
import Foundation
import QuartzCore
import SwiftUI

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
        let borderQuads = (borderQuads(gray, width: width, height: height, stride: stride, marksAreDark: false) +
                           borderQuads(gray, width: width, height: height, stride: stride, marksAreDark: true))
        let borderDetectionMilliseconds = (CACurrentMediaTime() - started) * 1000

        // The complete locator is measured from the border component, then
        // optimized using the printed Grid, Ring, Ray, and finder keys. No
        // generic rectangle model or fixed distance ladder participates.
        for quad in borderQuads.prefix(6) {
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
            let decodeStarted = CACurrentMediaTime()
            if let decoded = decodeAnyProfile(gray: gray, width: width, height: height, stride: stride,
                                              locatorQuad: locatorQuad, textQuad: textQuad, extendedQuad: extendedQuad) {
                publishDecoded(decoded.bytes, profile: decoded.profile,
                               detectionMilliseconds: borderDetectionMilliseconds,
                               decodeMilliseconds: (CACurrentMediaTime() - decodeStarted) * 1000,
                               candidates: borderQuads.count)
                return
            }
        }
        DispatchQueue.main.async { self.candidateSizes = borderQuads.isEmpty ? "" : "mathematical border candidates: \(borderQuads.count)" }
        publishProgress(borderQuads.isEmpty ? "Searching for CKey border and finder keys…" : "Calibrating CKey keys · \(lastDecodeDiagnostic)",
                        detectionMilliseconds: borderDetectionMilliseconds, candidates: borderQuads.count)
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
        // The continuous border may be measured at its inner or outer stroke.
        // Keep hypotheses distinct so each candidate can reach the same
        // canonical field from its measured boundary.
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

    private enum CalibrationField { case locator, text, extended }

    private func refinedCKeyQuad(_ observed: [CGPoint], gray: UnsafePointer<UInt8>,
                                 width: Int, height: Int, stride: Int,
                                 field: CalibrationField) -> [Double] {
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
        var bestScore = calibrationScore(gray, width: width, height: height, stride: stride, quad: best, field: field)
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
                let score = calibrationScore(gray, width: width, height: height, stride: stride, quad: candidate, field: field)
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
        // A tilted display is a projective quadrilateral, not a scaled square.
        // Move each measured corner independently against the physical key
        // evidence so keystone error is corrected before any data sample.
        var cornerStep = Double(side) * 0.035
        for _ in 0..<3 {
            for corner in 0..<4 {
                let baseX = corner * 2
                let baseY = baseX + 1
                for (dx, dy) in [(cornerStep, 0.0), (-cornerStep, 0.0), (0.0, cornerStep), (0.0, -cornerStep)] {
                    var candidate = best
                    candidate[baseX] += dx
                    candidate[baseY] += dy
                    let score = calibrationScore(gray, width: width, height: height, stride: stride, quad: candidate, field: field)
                    if score > bestScore {
                        best = candidate
                        bestScore = score
                    }
                }
            }
            cornerStep *= 0.5
        }
        return best
    }

    private func calibrationScore(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                                  stride: Int, quad: [Double], field: CalibrationField) -> Double {
        quad.withUnsafeBufferPointer { buffer in
            switch field {
            case .locator:
                return ckey_calibration_score_gray8(gray, width, height, stride, buffer.baseAddress)
            case .text:
                return ckey_calibration_score_text_gray8(gray, width, height, stride, buffer.baseAddress)
            case .extended:
                return ckey_calibration_score_extended_gray8(gray, width, height, stride, buffer.baseAddress)
            }
        }
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

    private func publishDecoded(_ payload: [UInt8], profile: String, detectionMilliseconds: Double,
                                decodeMilliseconds: Double, candidates: Int) {
        didDecode = true
        let hex = payload.map { String(format: "%02x", $0) }.joined()
        DispatchQueue.main.async {
            self.decodedHex = hex
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

    private func borderQuads(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                             stride: Int, marksAreDark: Bool) -> [[Double]] {
        // Work at half resolution: distant locator borders remain several
        // samples thick, while this keeps the connected-component pass small.
        let step = 2
        let sampledWidth = (width + step - 1) / step
        let sampledHeight = (height + step - 1) / step
        var visited = [UInt8](repeating: 0, count: sampledWidth * sampledHeight)
        var queue = [Int]()
        var candidates: [(quad: [Double], score: Double)] = []
        let minFrameSide = min(width, height)
        // Exposure and display brightness vary more than a fixed threshold
        // can tolerate.  Derive both polarity thresholds from robust image
        // percentiles; this is intentionally measured per frame rather than
        // tied to a camera distance or device profile.
        let threshold = adaptiveThreshold(gray, width: width, height: height, stride: stride,
                                          marksAreDark: marksAreDark)
        let isMark: (UInt8) -> Bool = { marksAreDark ? $0 <= threshold : $0 >= threshold }

        for sampleY in 0..<sampledHeight {
            for sampleX in 0..<sampledWidth {
                let start = sampleY * sampledWidth + sampleX
                if visited[start] != 0 { continue }
                let sourceX = min(sampleX * step, width - 1)
                let sourceY = min(sampleY * step, height - 1)
                // Allow a blurred or dim display border to remain a candidate;
                // the calibration keys, RS correction, and CRC are the false-
                // positive gate rather than this coarse luminance threshold.
                if !isMark(gray[sourceY * stride + sourceX]) { continue }

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
                            if isMark(gray[py * stride + px]) {
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
                // Fit an oriented quadrilateral to the continuous border
                // component.  The old bounding box erased rotation before
                // calibration and made every oblique scan start from the
                // wrong geometry.
                let points = queue.map { index -> (Double, Double) in
                    let x = (index % sampledWidth) * step
                    let y = (index / sampledWidth) * step
                    return (Double(min(x, width - 1)), Double(min(y, height - 1)))
                }
                let meanX = points.reduce(0.0) { $0 + $1.0 } / Double(points.count)
                let meanY = points.reduce(0.0) { $0 + $1.1 } / Double(points.count)
                let xx = points.reduce(0.0) { $0 + ($1.0 - meanX) * ($1.0 - meanX) }
                let yy = points.reduce(0.0) { $0 + ($1.1 - meanY) * ($1.1 - meanY) }
                let xy = points.reduce(0.0) { $0 + ($1.0 - meanX) * ($1.1 - meanY) }
                let angle = 0.5 * atan2(2.0 * xy, xx - yy)
                let ux = cos(angle), uy = sin(angle), vx = -uy, vy = ux
                var minU = Double.infinity, maxU = -Double.infinity
                var minV = Double.infinity, maxV = -Double.infinity
                for (x, y) in points {
                    let u = (x - meanX) * ux + (y - meanY) * uy
                    let v = (x - meanX) * vx + (y - meanY) * vy
                    minU = min(minU, u); maxU = max(maxU, u)
                    minV = min(minV, v); maxV = max(maxV, v)
                }
                func projected(_ u: Double, _ v: Double) -> Double { meanX + u * ux + v * vx }
                func projectedY(_ u: Double, _ v: Double) -> Double { meanY + u * uy + v * vy }
                let quad = [projected(minU, minV), projectedY(minU, minV),
                            projected(maxU, minV), projectedY(maxU, minV),
                            projected(maxU, maxV), projectedY(maxU, maxV),
                            projected(minU, maxV), projectedY(minU, maxV)]
                // The generated ranker merely orders mathematically measured
                // components. It cannot accept a symbol; key fit plus ECC/CRC
                // below are still required before the result is published.
                let probability = LocatorRanker.score(aspectError: abs(log(aspect)), fillRatio: fillRatio,
                                                       relativeSide: Double(side) / Double(minFrameSide))
                let score = 1.0 - probability
                candidates.append((quad: quad, score: score))
            }
        }
        return candidates.sorted { $0.score < $1.score }.map(\.quad)
    }

    private func adaptiveThreshold(_ gray: UnsafePointer<UInt8>, width: Int, height: Int,
                                   stride: Int, marksAreDark: Bool) -> UInt8 {
        var histogram = [Int](repeating: 0, count: 256)
        var samples = 0
        for y in Swift.stride(from: 0, to: height, by: 8) {
            for x in Swift.stride(from: 0, to: width, by: 8) {
                histogram[Int(gray[y * stride + x])] += 1
                samples += 1
            }
        }
        guard samples > 0 else { return marksAreDark ? 92 : 164 }
        func percentile(_ fraction: Double) -> Int {
            let target = Int(Double(samples - 1) * fraction)
            var total = 0
            for (value, count) in histogram.enumerated() {
                total += count
                if total > target { return value }
            }
            return 255
        }
        let low = percentile(0.10)
        let high = percentile(0.90)
        guard high - low >= 32 else { return marksAreDark ? 92 : 164 }
        let contrast = Double(high - low)
        let value = marksAreDark ? Double(low) + contrast * 0.32 : Double(high) - contrast * 0.32
        return UInt8(max(0, min(255, Int(value.rounded()))))
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
