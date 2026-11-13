import CKeyCodec
import CoreGraphics
import CoreVideo
import Foundation
import QuartzCore

/// The camera-independent half of the scanner: feed it frames, and it
/// decodes, tracks and advises. `CKeyCameraScanner` owns an
/// `AVCaptureSession` and uses one of these; a host with its own capture
/// pipeline ("bring your own camera") creates one directly.
///
///     let processor = CKeyFrameProcessor()
///     processor.onResult = { result in show(result) }          // main queue
///     processor.onTrack = { track in overlay.feed.publish(track) }
///     processor.onAdvice = { advice in control.apply(advice) }  // CKeyCameraControl, optional
///     // In captureOutput(_:didOutput:from:):
///     processor.process(pixelBuffer: buffer, captureTime: time)
///
/// What it does with each frame:
///
/// - **Decoding** (`ckey_scan_frame_accumulate_gray8`, or
///   `ckey_scan_frame_gray8` with `multiFrame` off), at most `maxFramesPerSecond`.
///   With `regionOfInterest` on, two of every three analysed frames search
///   only `scanRegion` plus an 18% margin, at full resolution and in place;
///   every third is searched whole, so codes outside the region still read.
///   The first valid decode goes to `onResult`; decoding then pauses until
///   `scanAgain()`.
/// - **Tracking** (`ckey_track_gray8`), on every frame and on its own queue,
///   separately from decoding, and it keeps going after a read. A located or
///   decoded scan re-seeds the track. Tracks go to `onTrack` on `trackQueue`.
/// - **Camera advice** (`ckey_camera_advise_gray8`), once per analysed frame:
///   exposure bias, zoom, the metering and focus point, and user guidance.
///
/// `process(pixelBuffer:captureTime:)` returns at once; the work happens on
/// two private queues, and a frame that arrives while one is busy is skipped
/// by that queue. All properties may be set from any thread.
///
/// Frames should be in the sensor's native orientation (do not rotate or
/// mirror the video data output's connection): normalized coordinates in
/// tracks and advice are then AVFoundation's capture-device coordinates.
public final class CKeyFrameProcessor {
    /// The default `scanRegion`: the middle of the frame.
    public static let defaultScanRegion = CGRect(x: 0.2, y: 0.1, width: 0.6, height: 0.8)
    /// Of every `wholeFrameEvery` analysed frames, one is searched whole.
    public static let wholeFrameEvery = 3
    /// The full reader (ML locator, corner refiner, site model, multi-frame
    /// averaging) runs only on a tracked code that the fast classical scan has
    /// failed `fullLadderAfterFailures` times in a row, then on one of every
    /// `fullLadderEvery` frames; with nothing tracked it never runs. Every
    /// other frame uses the fast scan, so a blurred frame costs tens of
    /// milliseconds instead of a second and the phone stays cool.
    public static let fullLadderEvery = 3
    /// Analysed frames per second while no code is tracked.
    public static let searchFramesPerSecond: Double = 8
    /// Fast scans of a tracked code that must fail before the full ladder runs.
    public static let fullLadderAfterFailures = 2

    // MARK: Settings

    /// Where the code is expected, as fractions of the frame (0...1): for a
    /// camera, the reticle converted with
    /// `AVCaptureVideoPreviewLayer.metadataOutputRectConverted(fromLayerRect:)`
    /// (see `CKeyPreviewView.sensorRect(fromViewRect:)`). `nil` searches
    /// whole frames only.
    public var scanRegion: CGRect? {
        get { sync { settings.scanRegion } }
        set {
            let region = newValue.map { $0.standardized.intersection(CGRect(x: 0, y: 0, width: 1, height: 1)) }
                .flatMap { $0.isNull || $0.width < 0.02 || $0.height < 0.02 ? nil : $0 }
            sync { settings.scanRegion = region }
        }
    }

    /// Region-of-interest decoding (default on): two of every three analysed
    /// frames search only `scanRegion` plus a margin. Off searches every
    /// frame whole.
    public var regionOfInterest: Bool {
        get { sync { settings.regionOfInterest } }
        set { sync { settings.regionOfInterest = newValue } }
    }

    /// Multi-frame reading (default on): a frame that does not decode is
    /// averaged with the previous frames of the same symbol (in its own
    /// coordinates, so the code may move), and the average is decoded. Frames
    /// that are each too noisy, glared or partly hidden can read together.
    /// Reed-Solomon and CRC-32C still decide; `CKeyScanOutcome.accumulated`
    /// marks such a read.
    public var multiFrame: Bool {
        get { sync { settings.multiFrame } }
        set { sync { settings.multiFrame = newValue; pendingAccumulatorReset = true } }
    }

    /// Per-frame tracking (default on). Off stops the tracker and reports the
    /// track as lost; `onTrack` is then never called with a track.
    public var tracking: Bool {
        get { sync { settings.tracking } }
        set {
            sync { settings.tracking = newValue }
            if !newValue { dropTrack() }
        }
    }

    /// Frame-rate cap for decoding (default 20); 0 analyses every frame the
    /// decode queue is free for. Tracking is not capped.
    public var maxFramesPerSecond: Double {
        get { sync { settings.maxFramesPerSecond } }
        set { sync { settings.maxFramesPerSecond = newValue } }
    }

    /// Called on `callbackQueue` with the first valid decode after creation
    /// or `scanAgain()`.
    public var onResult: ((CKeyResult) -> Void)? {
        get { sync { settings.onResult } }
        set { sync { settings.onResult = newValue } }
    }

    /// Called on `trackQueue` with the tracked symbol for each frame it was
    /// measured on, and with `nil` once the track is lost.
    public var onTrack: ((CKeyTrack?) -> Void)? {
        get { sync { settings.onTrack } }
        set { sync { settings.onTrack = newValue } }
    }

    /// The queue `onTrack` is called on (default main). A custom overlay that
    /// draws from a display link can take tracks on any queue and keep the
    /// latest behind a lock (see `CKeyTrackFeed`).
    public var trackQueue: DispatchQueue {
        get { sync { settings.trackQueue } }
        set { sync { settings.trackQueue = newValue } }
    }

    /// Called with the core's advice for every analysed frame, on the
    /// processor's analysis queue: return quickly. Pass it to
    /// `CKeyCameraControl.apply(_:)` to let the SDK drive a capture device.
    public var onAdvice: ((CKeyCameraAdvice) -> Void)? {
        get { sync { settings.onAdvice } }
        set { sync { settings.onAdvice = newValue } }
    }

    /// Called on `callbackQueue` when what the user should be told changes
    /// (an empty set clears the hint). Flags are steadied over a few frames
    /// so a hint does not flicker.
    public var onGuidance: ((CKeyGuidance) -> Void)? {
        get { sync { settings.onGuidance } }
        set { sync { settings.onGuidance = newValue } }
    }

    /// The queue for `onResult` and `onGuidance` (default main).
    public var callbackQueue: DispatchQueue {
        get { sync { settings.callbackQueue } }
        set { sync { settings.callbackQueue = newValue } }
    }

    private struct Settings {
        var scanRegion: CGRect? = CKeyFrameProcessor.defaultScanRegion
        var regionOfInterest = true
        var multiFrame = true
        var tracking = true
        var maxFramesPerSecond: Double = 20
        var onResult: ((CKeyResult) -> Void)?
        var onTrack: ((CKeyTrack?) -> Void)?
        var trackQueue = DispatchQueue.main
        var onAdvice: ((CKeyCameraAdvice) -> Void)?
        var onGuidance: ((CKeyGuidance) -> Void)?
        var callbackQueue = DispatchQueue.main
    }

    /// A scan's candidate, handed from the analysis queue to the tracker.
    private struct Seed {
        let quad: [Double]
        let score: Double
        let decoded: Bool
        let width: Int
        let height: Int
    }

    private struct Analysis {
        let outcome: CKeyScanOutcome
        let advice: CKeyCameraAdvice?
        let usedRegion: Bool
    }

    private let observer: CKeyScanObserver?
    private let analysisQueue = DispatchQueue(label: "org.openckey.scanner.analysis", qos: .userInitiated)
    private let trackWorkQueue = DispatchQueue(label: "org.openckey.scanner.track", qos: .userInteractive)

    // Guarded by `lock`.
    private let lock = NSLock()
    private var settings = Settings()
    private var analysisBusy = false
    private var trackBusy = false
    private var didDecode = false
    private var lastAnalysisTime: CFTimeInterval = 0
    private var frameIndex = 0
    private var pendingSeed: Seed?
    private var pendingTrackReset = false
    private var pendingZoomRatio = 1.0
    private var pendingPolicyReset = false
    /// The locked track's corners in pixels, for camera advice between scans.
    private var trackedQuad: Seed?

    // Analysis queue only.
    private var policy = ckey_camera_policy()
    private var payload = [UInt8](repeating: 0, count: CKeyDecoder.payloadCapacity)
    /// Analysis queue only: a pending diagnostic capture.
    private var diagnosticWanted = 0
    private var diagnosticFrames: [CKeyDiagnosticFrame] = []
    private var diagnosticDone: (@Sendable ([CKeyDiagnosticFrame]) -> Void)?
    // Analysis-queue state: the multi-frame accumulator (core-owned).
    private let accumulator: OpaquePointer? = ckey_accumulator_create()
    // Guarded by lock: restart the accumulator before the next analysis.
    private var pendingAccumulatorReset = false
    private var stabilizer = CKeyGuidanceStabilizer()
    private var lastWholeFrameCut = false
    /// Analysis queue only: consecutive failed scans of the tracked code.
    private var trackedFailures = 0

    // Track queue only.
    private var track = ckey_track_state()
    private var lastPublished: [CGPoint]?
    private var wasLocked = false
    private var lastAcquisition: TimeInterval = -1
    /// With no track, the whole frame is searched for one at most this often;
    /// scans seed the track in between.
    private static let acquisitionInterval: TimeInterval = 0.2

    /// - Parameter observer: optional scan-session observer (frame timings
    ///   and the first decode); held strongly. Session begin and end are the
    ///   caller's to report.
    public init(observer: CKeyScanObserver? = nil) {
        self.observer = observer
        ckey_camera_policy_init(&policy)
        ckey_track_init(&track)
    }

    deinit {
        ckey_accumulator_destroy(accumulator)
    }

    @discardableResult
    private func sync<T>(_ body: () -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body()
    }

    // MARK: Diagnostics

    /// Keeps the next `count` analysed frames (luma cropped to the area each
    /// scan read, at full resolution, with the corners found) and hands them
    /// to `completion` on the main queue. For diagnosing frames that do not
    /// read; nothing leaves the device unless the host shares the result.
    public func captureDiagnostics(count: Int = 12, completion: @escaping @Sendable ([CKeyDiagnosticFrame]) -> Void) {
        analysisQueue.async {
            self.diagnosticFrames = []
            self.diagnosticWanted = max(1, min(60, count))
            self.diagnosticDone = completion
        }
    }

    private func keepDiagnostic(_ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int, _ stride: Int,
                                region: CGRect?, outcome: CKeyScanOutcome, held: Seed?, fast: Bool,
                                milliseconds: Double, index: Int) {
        guard diagnosticWanted > 0 else { return }
        var crop = region ?? CGRect(x: 0, y: 0, width: width, height: height)
        // Whole frames are kept at most 2,048 pixels on a side, centred.
        if region == nil, max(width, height) > 2048 {
            let w = min(width, 2048), h = min(height, 2048)
            crop = CGRect(x: (width - w) / 2, y: (height - h) / 2, width: w, height: h)
        }
        let x0 = Int(crop.minX), y0 = Int(crop.minY), w = Int(crop.width), h = Int(crop.height)
        guard w > 0, h > 0, x0 >= 0, y0 >= 0, x0 + w <= width, y0 + h <= height else { return }
        var bytes = Data(count: w * h)
        bytes.withUnsafeMutableBytes { raw in
            let out = raw.bindMemory(to: UInt8.self)
            for y in 0..<h { for x in 0..<w { out[y * w + x] = gray[(y0 + y) * stride + x0 + x] } }
        }
        func local(_ q: [Double]) -> [Double] { q.enumerated().map { $0.offset % 2 == 0 ? $0.element - Double(x0) : $0.element - Double(y0) } }
        let found = outcome.located && outcome.quad.count == 4 ? local(CKeyCorners.flat(outcome.quad)) : []
        let tracked = held.map { $0.width == width && $0.height == height ? local($0.quad) : [] } ?? []
        diagnosticFrames.append(CKeyDiagnosticFrame(gray: bytes, width: w, height: h, cropX: x0, cropY: y0,
                                                    frameWidth: width, frameHeight: height, quad: found, trackedQuad: tracked,
                                                    decoded: outcome.result != nil, fast: fast, milliseconds: milliseconds, index: index))
        diagnosticWanted -= 1
        // A decode stops analysis: hand over what was kept up to it.
        if outcome.result != nil { diagnosticWanted = 0 }
        if diagnosticWanted == 0, let done = diagnosticDone {
            let frames = diagnosticFrames
            diagnosticFrames = []
            diagnosticDone = nil
            DispatchQueue.main.async { done(frames) }
        }
    }

    // MARK: Feeding frames

    /// Hands a frame to the tracker and, at most `maxFramesPerSecond`, to the
    /// decoder. Returns at once; call it for every frame from the capture
    /// callback. Supported formats are those of `CKeyDecoder.decode(pixelBuffer:)`;
    /// the bi-planar 4:2:0 formats are read in place.
    /// - Parameter captureTime: when the frame was captured, in host-clock
    ///   seconds: its presentation timestamp converted from the session's
    ///   synchronization clock (`CKeyCameraScanner.captureTime(of:session:)`).
    public func process(pixelBuffer: CVPixelBuffer, captureTime: TimeInterval) {
        let now = CACurrentMediaTime()
        lock.lock()
        let runTrack = settings.tracking && !trackBusy
        if runTrack { trackBusy = true }
        // Until a code is tracked, look a few times a second only: nothing in
        // view costs little power. While tracked, up to maxFramesPerSecond.
        let limit = trackedQuad == nil ? min(Self.searchFramesPerSecond, settings.maxFramesPerSecond > 0 ? settings.maxFramesPerSecond : Self.searchFramesPerSecond)
                                       : settings.maxFramesPerSecond
        let runAnalysis = !didDecode && !analysisBusy && (limit <= 0 || now - lastAnalysisTime >= 1 / limit)
        if runAnalysis {
            analysisBusy = true
            lastAnalysisTime = now
            frameIndex &+= 1
        }
        let index = frameIndex
        lock.unlock()

        if runTrack {
            trackWorkQueue.async {
                self.trackFrame(pixelBuffer, captureTime: captureTime)
                self.sync { self.trackBusy = false }
            }
        }
        if runAnalysis {
            analysisQueue.async {
                self.analyseFrame(pixelBuffer, index: index)
                self.sync { self.analysisBusy = false }
            }
        }
    }

    /// Processes an 8-bit grayscale frame (`stride` bytes per row) before
    /// returning: one tracking step, then one scan with advice, with the same
    /// callbacks as `process(pixelBuffer:captureTime:)`. No frame-rate cap
    /// applies. Returns the scan, or `nil` while waiting for `scanAgain()`
    /// after a decode. Do not call it from `onAdvice`.
    @discardableResult
    public func process(gray: UnsafePointer<UInt8>, width: Int, height: Int, stride: Int,
                        captureTime: TimeInterval) -> CKeyScanOutcome? {
        guard width >= 16, height >= 16, stride >= width else { return nil }
        if sync({ settings.tracking }) {
            trackWorkQueue.sync {
                let control = self.takeTrackControl()
                if control.reset { self.resetTrack() }
                self.trackStep(gray, width, height, stride, seed: control.seed, zoomRatio: control.zoomRatio,
                               captureTime: captureTime)
            }
        }
        let index: Int? = sync {
            if didDecode { return nil }
            frameIndex &+= 1
            return frameIndex
        }
        guard let index else { return nil }
        return analysisQueue.sync { () -> CKeyScanOutcome in
            let started = CACurrentMediaTime()
            let analysis = self.analyse(gray, width, height, stride, index: index)
            self.finish(analysis, started: started)
            return analysis.outcome
        }
    }

    /// Accepts the next decode again, drops the track and restarts the
    /// exposure policy. Call it when the user is ready for another code.
    public func scanAgain() {
        sync {
            didDecode = false
            pendingPolicyReset = true
            pendingAccumulatorReset = true
            pendingSeed = nil
        }
        dropTrack()
    }

    /// Drops the track (reporting it lost) and clears guidance, without
    /// changing whether a decode is accepted. Call it when the camera stops.
    public func reset() {
        sync {
            pendingPolicyReset = true
            pendingAccumulatorReset = true
            pendingSeed = nil
        }
        dropTrack()
        analysisQueue.async { self.clearGuidance() }
    }

    /// Tells the tracker that the camera's zoom changed by `ratio` (new zoom
    /// divided by old), so the track follows the symbol to where it lands in
    /// the zoomed frame instead of losing it. `CKeyCameraControl` reports
    /// this through `onZoomChange`.
    public func zoomDidChange(by ratio: Double) {
        guard ratio.isFinite, ratio > 0 else { return }
        sync {
            pendingZoomRatio *= ratio
            pendingSeed = nil  // measured at the old zoom
        }
    }

    // MARK: Tracking (track queue)

    private func dropTrack() {
        sync { pendingTrackReset = true }
        trackWorkQueue.async {
            if self.sync({ () -> Bool in
                let reset = self.pendingTrackReset
                self.pendingTrackReset = false
                return reset
            }) {
                self.resetTrack()
            }
        }
    }

    private func takeTrackControl() -> (seed: Seed?, reset: Bool, zoomRatio: Double) {
        sync {
            let control = (seed: pendingSeed, reset: pendingTrackReset, zoomRatio: pendingZoomRatio)
            pendingSeed = nil
            pendingTrackReset = false
            pendingZoomRatio = 1
            return control
        }
    }

    private func resetTrack() {
        let gate = track.gate, lockScore = track.lock_score
        ckey_track_init(&track)
        if gate > 0 { track.gate = gate }
        if lockScore > 0 { track.lock_score = lockScore }
        lastAcquisition = -1
        reportLoss()
    }

    private func reportLoss() {
        sync { trackedQuad = nil }
        guard wasLocked else { return }
        wasLocked = false
        lastPublished = nil
        deliver(nil)
    }

    private func deliver(_ track: CKeyTrack?) {
        let (handler, queue) = sync { (settings.onTrack, settings.trackQueue) }
        guard let handler else { return }
        queue.async { handler(track) }
    }

    private func trackFrame(_ pixelBuffer: CVPixelBuffer, captureTime: TimeInterval) {
        let control = takeTrackControl()
        if control.reset { resetTrack() }
        guard sync({ settings.tracking }) else { return }
        _ = CKeyLuma.withGray(pixelBuffer) { gray, width, height, stride -> Bool in
            self.trackStep(gray, width, height, stride, seed: control.seed, zoomRatio: control.zoomRatio,
                           captureTime: captureTime)
            return true
        }
    }

    /// One tracker step on a frame. The detection, continuity checks, lock
    /// rules and corner order are the core's (`ckey_track_gray8`).
    private func trackStep(_ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int, _ stride: Int,
                           seed: Seed?, zoomRatio: Double, captureTime: TimeInterval) {
        guard width >= 32, height >= 32, stride >= width else { return }
        if zoomRatio != 1, track.status != CKEY_TRACK_NONE {
            // The zoom is centred on the frame: move the last corners with it.
            var quad = ckeyQuadArray(track.quad)
            let centreX = Double(width) / 2, centreY = Double(height) / 2
            for corner in 0..<4 {
                quad[2 * corner] = centreX + (quad[2 * corner] - centreX) * zoomRatio
                quad[2 * corner + 1] = centreY + (quad[2 * corner + 1] - centreY) * zoomRatio
            }
            track.quad = ckeyQuadTuple(quad)
        }
        if let seed, seed.width == width, seed.height == height, seed.quad.count == 8, shouldSeed(seed) {
            ckey_track_seed(&track, seed.quad, seed.score, seed.decoded ? 1 : 0)
        }
        if track.status == CKEY_TRACK_NONE {
            // No track: the core searches the whole frame. Do that a few times
            // a second only; scans seed the track in between.
            if lastAcquisition >= 0, captureTime >= lastAcquisition,
               captureTime - lastAcquisition < Self.acquisitionInterval {
                reportLoss()
                return
            }
            lastAcquisition = captureTime
        }
        let status = ckey_track_gray8(gray, width, height, stride, &track)
        guard status == CKEY_TRACK_LOCKED else {
            reportLoss()
            return
        }
        let quad = ckeyQuadArray(track.quad)
        let held = Seed(quad: quad, score: track.score, decoded: track.decoded != 0, width: width, height: height)
        sync { trackedQuad = held }
        // A missed frame holds the last corners: nothing new to report.
        guard track.fresh != 0 else { return }
        var corners = (0..<4).map { corner in
            CGPoint(x: quad[2 * corner] / Double(width), y: quad[2 * corner + 1] / Double(height))
        }
        // The core keeps the order within a track; this keeps it across a
        // re-seed too, so an overlay never twists.
        if let last = lastPublished { corners = CKeyCorners.matched(corners, to: last) }
        lastPublished = corners
        wasLocked = true
        deliver(CKeyTrack(corners: corners, captureTime: captureTime, score: track.score, decoded: track.decoded != 0))
    }

    /// Whether a scan's candidate should re-seed the track. A track that is
    /// already locked on the same symbol is measured on every frame and is
    /// newer than the scan, so it is re-seeded only to mark it decoded.
    private func shouldSeed(_ seed: Seed) -> Bool {
        guard track.status == CKEY_TRACK_LOCKED else { return true }
        let current = ckeyQuadArray(track.quad)
        var centreX = 0.0, centreY = 0.0, seedX = 0.0, seedY = 0.0, side = 0.0
        for corner in 0..<4 {
            centreX += current[2 * corner] / 4
            centreY += current[2 * corner + 1] / 4
            seedX += seed.quad[2 * corner] / 4
            seedY += seed.quad[2 * corner + 1] / 4
            let next = (corner + 1) % 4
            side += hypot(current[2 * next] - current[2 * corner], current[2 * next + 1] - current[2 * corner + 1]) / 4
        }
        let sameSymbol = hypot(seedX - centreX, seedY - centreY) < 0.5 * side
        if sameSymbol { return seed.decoded && track.decoded == 0 }
        return seed.decoded || seed.score > track.score
    }

    // MARK: Decoding and advice (analysis queue)

    private func analyseFrame(_ pixelBuffer: CVPixelBuffer, index: Int) {
        let started = CACurrentMediaTime()
        let analysis = CKeyLuma.withGray(pixelBuffer) { gray, width, height, stride -> Analysis in
            self.analyse(gray, width, height, stride, index: index)
        }
        guard let analysis else { return }
        finish(analysis, started: started)
    }

    private func analyse(_ gray: UnsafePointer<UInt8>, _ width: Int, _ height: Int, _ stride: Int,
                         index: Int) -> Analysis {
        let (region, useRegion, held, resetPolicy, multiFrame, resetAccumulator) = sync { () -> (CGRect?, Bool, Seed?, Bool, Bool, Bool) in
            let reset = pendingPolicyReset, resetAcc = pendingAccumulatorReset
            pendingPolicyReset = false
            pendingAccumulatorReset = false
            return (settings.scanRegion, settings.regionOfInterest, trackedQuad, reset, settings.multiFrame, resetAcc)
        }
        if resetPolicy { ckey_camera_policy_init(&policy) }
        if resetAccumulator { ckey_accumulator_reset(accumulator) }
        // While the tracker holds the code, read around it (its corners plus a
        // margin) on every frame: the code is wherever the brackets are, even
        // when it is larger than the reticle, which a reticle region would cut.
        // Otherwise two of every three analysed frames read only the reticle
        // region, at full resolution and in place, and every third is read whole.
        var pixelRegion: CGRect?
        if let held, held.width == width, held.height == height,
           let around = Self.trackedRegion(held.quad, width: width, height: height) {
            pixelRegion = around
        } else if useRegion, let region, index % Self.wholeFrameEvery != 0 {
            pixelRegion = CKeyDecoder.pixelRegion(normalized: region, width: width, height: height)
        }
        let tracked = held != nil && held?.width == width && held?.height == height
        if !tracked { trackedFailures = 0 }
        let fast = !(tracked && trackedFailures >= Self.fullLadderAfterFailures && index % Self.fullLadderEvery == 0)
        let scanStarted = CACurrentMediaTime()
        let outcome = CKeyDecoder.scan(gray: gray, width: width, height: height, stride: stride,
                                       region: pixelRegion, payload: &payload,
                                       accumulator: multiFrame ? accumulator : nil, fast: fast)
        if tracked { trackedFailures = outcome.result == nil ? trackedFailures + 1 : 0 }
        keepDiagnostic(gray, width, height, stride, region: pixelRegion, outcome: outcome, held: held, fast: fast,
                       milliseconds: (CACurrentMediaTime() - scanStarted) * 1000, index: index)

        // Advice on the scan's candidate, or on the tracked symbol between scans.
        var quad: [Double]?
        if outcome.located, outcome.quad.count == 4 {
            quad = CKeyCorners.flat(outcome.quad)
        } else if let held, held.width == width, held.height == height {
            quad = held.quad
        }
        var advice = ckey_camera_advice()
        let decoded: Int32 = outcome.result != nil ? 1 : 0
        let status: ckey_status
        if let quad {
            status = ckey_camera_advise_gray8(gray, width, height, stride, quad, decoded, &policy, &advice)
        } else {
            status = ckey_camera_advise_gray8(gray, width, height, stride, nil, decoded, &policy, &advice)
        }
        var swiftAdvice: CKeyCameraAdvice?
        if status == CKEY_OK {
            let corners = advice.has_candidate != 0 ? (quad ?? []) : []
            swiftAdvice = CKeyCameraAdvice(
                hasCandidate: advice.has_candidate != 0,
                guidance: CKeyGuidance(rawValue: advice.flags).intersection(.all),
                reaim: advice.flags & CKEY_ADVICE_REAIM != 0,
                point: CGPoint(x: advice.point_x, y: advice.point_y),
                size: advice.size,
                clippedFraction: advice.clipped_fraction,
                contrast: advice.contrast,
                sharpness: advice.sharpness,
                exposureBiasEV: advice.exposure_bias_ev,
                zoomFactor: advice.zoom_factor,
                corners: CKeyCorners.points(corners).map {
                    CGPoint(x: $0.x / CGFloat(width), y: $0.y / CGFloat(height))
                })
        }
        return Analysis(outcome: outcome, advice: swiftAdvice, usedRegion: pixelRegion != nil)
    }

    /// The tracked code's bounding box grown by a quarter of its size on each
    /// side, in whole pixels and clamped to the frame; `nil` if degenerate.
    public static func trackedRegion(_ quad: [Double], width: Int, height: Int) -> CGRect? {
        guard quad.count == 8, quad.allSatisfy({ $0.isFinite }) else { return nil }
        let xs = stride(from: 0, to: 8, by: 2).map { quad[$0] }, ys = stride(from: 1, to: 8, by: 2).map { quad[$0] }
        guard let minX = xs.min(), let maxX = xs.max(), let minY = ys.min(), let maxY = ys.max() else { return nil }
        let pad = 0.25 * max(maxX - minX, maxY - minY)
        let x0 = max(0, Int((minX - pad).rounded(.down))), y0 = max(0, Int((minY - pad).rounded(.down)))
        let x1 = min(width, Int((maxX + pad).rounded(.up))), y1 = min(height, Int((maxY + pad).rounded(.up)))
        guard x1 - x0 >= 64, y1 - y0 >= 64 else { return nil }
        return CGRect(x: x0, y: y0, width: x1 - x0, height: y1 - y0)
    }

    private func finish(_ analysis: Analysis, started: CFTimeInterval) {
        observer?.scanFrameProcessed(milliseconds: (CACurrentMediaTime() - started) * 1000)
        let outcome = analysis.outcome
        let snapshot: Settings = sync {
            if outcome.located, outcome.quad.count == 4 {
                pendingSeed = Seed(quad: CKeyCorners.flat(outcome.quad), score: outcome.score,
                                   decoded: outcome.result != nil,
                                   width: Int(outcome.frameSize.width), height: Int(outcome.frameSize.height))
            }
            return settings
        }
        if let advice = analysis.advice { snapshot.onAdvice?(advice) }

        if let result = outcome.result {
            let first: Bool = sync {
                if didDecode { return false }
                didDecode = true
                return true
            }
            clearGuidance()
            guard first else { return }
            observer?.scanSessionDidDecode(result)
            if let onResult = snapshot.onResult {
                snapshot.callbackQueue.async { onResult(result) }
            }
            return
        }

        // A border cut by the region's edge is not the user's problem (whole
        // frames read a code larger than the region), so only whole frames
        // set the flag; region frames in between keep the last answer.
        if !analysis.usedRegion { lastWholeFrameCut = outcome.symbolCut }
        var flags = analysis.advice?.guidance ?? []
        if lastWholeFrameCut { flags.insert(.moveBack) }
        if let changed = stabilizer.update(flags), let onGuidance = snapshot.onGuidance {
            snapshot.callbackQueue.async { onGuidance(changed) }
        }
    }

    private func clearGuidance() {
        lastWholeFrameCut = false
        guard stabilizer.reset() else { return }
        let (handler, queue) = sync { (settings.onGuidance, settings.callbackQueue) }
        if let handler { queue.async { handler([]) } }
    }
}

/// One analysed camera frame kept for diagnosis (`CKeyFrameProcessor.captureDiagnostics`):
/// the 8-bit luma the scanner read, cropped to the area it scanned, and what it found.
public struct CKeyDiagnosticFrame: Sendable {
    /// Row-major luma, `width` x `height`, one byte per pixel.
    public let gray: Data
    public let width: Int
    public let height: Int
    /// Where the crop sits in the full frame, in pixels.
    public let cropX: Int
    public let cropY: Int
    /// The full frame's size, in pixels.
    public let frameWidth: Int
    public let frameHeight: Int
    /// The candidate's four corners (x, y pairs) in crop pixels, when the scan located one.
    public let quad: [Double]
    /// The tracked corners in crop pixels, when the tracker held the code.
    public let trackedQuad: [Double]
    public let decoded: Bool
    /// True for a fast (classical-only) scan, false for the full reader.
    public let fast: Bool
    public let milliseconds: Double
    public let index: Int
}
