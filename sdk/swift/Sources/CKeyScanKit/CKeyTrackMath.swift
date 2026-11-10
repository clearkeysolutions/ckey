import CKeyCodec
import CoreGraphics
import Foundation

// The pure parts of the camera layer: track and advice value types, corner
// utilities, and the bracket motion (One Euro filter, latency projection,
// critically damped spring). Nothing here touches UIKit or AVFoundation, so
// it builds and is tested on macOS.

// MARK: - Track

/// A tracked symbol on one camera frame: the raw data for a custom overlay.
/// `CKeyBracketOverlay` draws the built-in brackets from the same values.
public struct CKeyTrack: Equatable, Sendable {
    /// The symbol's four field corners as fractions of the frame (0...1, x
    /// right, y down). For frames in the sensor's native orientation these
    /// are AVFoundation's capture-device coordinates; convert them to view
    /// points with `CKeyPreviewView.viewPoints(fromSensorPoints:)`. The order
    /// is stable from frame to frame, so an outline never twists.
    public let corners: [CGPoint]
    /// When the frame was captured, in host-clock seconds (the clock of
    /// `CACurrentMediaTime()` and of `CADisplayLink` timestamps).
    public let captureTime: TimeInterval
    /// The fit score of the corners against the CKey keys.
    public let score: Double
    /// True once this symbol has decoded.
    public let decoded: Bool

    public init(corners: [CGPoint], captureTime: TimeInterval, score: Double, decoded: Bool) {
        self.corners = corners
        self.captureTime = captureTime
        self.score = score
        self.decoded = decoded
    }
}

/// The latest track behind a lock: written from the scanner's `onTrack` on
/// any queue, read by an overlay's display link on the main thread. Corners
/// reach `CKeyBracketOverlay` through one of these, never through SwiftUI
/// state, so the brackets do not wait on a view update.
public final class CKeyTrackFeed: @unchecked Sendable {
    private let lock = NSLock()
    private var latest: CKeyTrack?

    public init() {}

    /// Stores the latest track; `nil` (or a track without four corners) clears it.
    public func publish(_ track: CKeyTrack?) {
        lock.lock()
        defer { lock.unlock() }
        latest = track.flatMap { $0.corners.count == 4 ? $0 : nil }
    }

    /// The latest track, or `nil` when there is none.
    public var current: CKeyTrack? {
        lock.lock()
        defer { lock.unlock() }
        return latest
    }
}

// MARK: - Guidance and advice

/// What to tell the user, from the core's camera advice (`CKEY_ADVICE_*`).
public struct CKeyGuidance: OptionSet, Hashable, Sendable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }

    /// The code is too small in the frame: move closer.
    public static let moveCloser = CKeyGuidance(rawValue: 1)
    /// A corner of the code is cut off by the frame edge: move back.
    public static let moveBack = CKeyGuidance(rawValue: 2)
    /// The image is blurred: hold steady.
    public static let holdSteady = CKeyGuidance(rawValue: 4)
    /// The code is out of focus (the camera is asked to refocus when the SDK
    /// controls it; otherwise the user may need to move back a little).
    public static let refocus = CKeyGuidance(rawValue: 8)
    /// Part of the code is washed out by glare: tilt the phone or the surface.
    public static let glare = CKeyGuidance(rawValue: 16)

    /// Every guidance flag.
    public static let all: CKeyGuidance = [.moveCloser, .moveBack, .holdSteady, .refocus, .glare]

    /// The single most useful hint to show when several are set: a cut code
    /// first, then glare, focus, blur and size.
    public var primary: CKeyGuidance? {
        for flag in [CKeyGuidance.moveBack, .glare, .refocus, .holdSteady, .moveCloser] where contains(flag) {
            return flag
        }
        return nil
    }
}

/// The core's per-frame camera advice (`ckey_camera_advice`). The capture
/// policy lives in the core so every platform behaves the same; a camera
/// layer applies what it returns. `CKeyCameraControl` does that for an
/// `AVCaptureDevice`.
public struct CKeyCameraAdvice: Equatable, Sendable {
    /// True when the advice is about a located or tracked candidate.
    public let hasCandidate: Bool
    /// What to tell the user, if anything.
    public let guidance: CKeyGuidance
    /// The metering point moved enough to re-aim exposure and focus at `point`.
    public let reaim: Bool
    /// Where to meter and focus, as fractions of the frame: the candidate's
    /// centre, or the frame centre without one.
    public let point: CGPoint
    /// Candidate side divided by the frame's shorter side; 0 without one.
    public let size: Double
    /// Share of the candidate at or near white clipping.
    public let clippedFraction: Double
    /// Michelson contrast inside the candidate, 0...1.
    public let contrast: Double
    /// 0 (soft) to 1 (sharp) at the frame stroke.
    public let sharpness: Double
    /// Exposure target bias to set, in EV (0, or negative when the symbol
    /// clips or while nothing decodes).
    public let exposureBiasEV: Double
    /// Multiply the current zoom by this (1 = keep). Apply it only up to the
    /// device's real-detail limit.
    public let zoomFactor: Double
    /// The candidate's corners as fractions of the frame; empty without one.
    public let corners: [CGPoint]

    public init(hasCandidate: Bool, guidance: CKeyGuidance, reaim: Bool, point: CGPoint, size: Double,
                clippedFraction: Double, contrast: Double, sharpness: Double, exposureBiasEV: Double,
                zoomFactor: Double, corners: [CGPoint] = []) {
        self.hasCandidate = hasCandidate
        self.guidance = guidance
        self.reaim = reaim
        self.point = point
        self.size = size
        self.clippedFraction = clippedFraction
        self.contrast = contrast
        self.sharpness = sharpness
        self.exposureBiasEV = exposureBiasEV
        self.zoomFactor = zoomFactor
        self.corners = corners
    }
}

/// Keeps guidance from flickering: a flag is shown after it has been set on
/// `onAfter` analysed frames in a row and hidden after `offAfter` frames in a
/// row without it.
public struct CKeyGuidanceStabilizer: Sendable {
    public var onAfter: Int
    public var offAfter: Int
    /// The guidance currently shown.
    public private(set) var shown: CKeyGuidance = []
    private var streaks = [Int](repeating: 0, count: 5)
    private static let flags: [CKeyGuidance] = [.moveCloser, .moveBack, .holdSteady, .refocus, .glare]

    public init(onAfter: Int = 3, offAfter: Int = 6) {
        self.onAfter = max(1, onAfter)
        self.offAfter = max(1, offAfter)
    }

    /// Feeds one frame's flags. Returns the new shown set when it changed.
    public mutating func update(_ flags: CKeyGuidance) -> CKeyGuidance? {
        var next = shown
        for (index, flag) in Self.flags.enumerated() {
            let set = flags.contains(flag)
            if set == shown.contains(flag) {
                streaks[index] = 0
                continue
            }
            streaks[index] += 1
            if streaks[index] >= (set ? onAfter : offAfter) {
                streaks[index] = 0
                if set { next.insert(flag) } else { next.remove(flag) }
            }
        }
        guard next != shown else { return nil }
        shown = next
        return next
    }

    /// Clears everything (a new scan session). Returns true if something was shown.
    public mutating func reset() -> Bool {
        let had = !shown.isEmpty
        shown = []
        streaks = [Int](repeating: 0, count: 5)
        return had
    }
}

// MARK: - Corner utilities

/// Geometry helpers for sets of four corners.
public enum CKeyCorners {
    /// `corners` reordered (cyclically, either winding) to the order closest
    /// to `previous`, by the core's `ckey_match_corner_order`. Use it before
    /// comparing or smoothing corner sets from different sources. Returns
    /// `corners` unchanged unless both sets hold four finite points.
    public static func matched(_ corners: [CGPoint], to previous: [CGPoint]) -> [CGPoint] {
        guard corners.count == 4, previous.count == 4 else { return corners }
        let reference = flat(previous)
        var quad = flat(corners)
        guard reference.allSatisfy({ $0.isFinite }), quad.allSatisfy({ $0.isFinite }) else { return corners }
        ckey_match_corner_order(reference, &quad)
        return points(quad)
    }

    /// The mean of the points; `.zero` for none.
    public static func centroid(_ corners: [CGPoint]) -> CGPoint {
        guard !corners.isEmpty else { return .zero }
        let count = CGFloat(corners.count)
        return CGPoint(x: corners.reduce(0) { $0 + $1.x } / count, y: corners.reduce(0) { $0 + $1.y } / count)
    }

    /// The smallest upright rectangle holding the points; `.null` for none.
    public static func boundingBox(_ corners: [CGPoint]) -> CGRect {
        guard let first = corners.first else { return .null }
        var minX = first.x, maxX = first.x, minY = first.y, maxY = first.y
        for point in corners.dropFirst() {
            minX = min(minX, point.x); maxX = max(maxX, point.x)
            minY = min(minY, point.y); maxY = max(maxY, point.y)
        }
        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }

    /// The corners pushed out from their centre by `padding` (0.10 = 10%),
    /// and further if needed so the centre-to-corner span is at least
    /// `minimumSpan`: brackets then sit just outside the code and never
    /// collapse on a tiny one. Returns `nil` unless there are four corners
    /// more than a point apart.
    public static func expanded(_ corners: [CGPoint], padding: CGFloat, minimumSpan: CGFloat = 0) -> [CGPoint]? {
        guard corners.count == 4 else { return nil }
        let centre = centroid(corners)
        let span = corners.map { hypot($0.x - centre.x, $0.y - centre.y) }.max() ?? 0
        guard span > 1 else { return nil }
        let scale = max(1 + padding, minimumSpan / span)
        return corners.map { CGPoint(x: centre.x + ($0.x - centre.x) * scale, y: centre.y + ($0.y - centre.y) * scale) }
    }

    /// The corners after a zoom by `ratio` about `centre`: where a symbol
    /// lands in the frame when the camera zooms by that factor.
    public static func scaled(_ corners: [CGPoint], by ratio: CGFloat,
                              about centre: CGPoint = CGPoint(x: 0.5, y: 0.5)) -> [CGPoint] {
        corners.map { CGPoint(x: centre.x + ($0.x - centre.x) * ratio, y: centre.y + ($0.y - centre.y) * ratio) }
    }

    /// The largest zoom ratio (about the frame centre, normalized
    /// coordinates) that keeps every corner at least `inset` inside the
    /// frame. At least 1 when the corners are inside already.
    public static func largestZoomKeepingInFrame(_ corners: [CGPoint], inset: CGFloat = 0.03) -> CGFloat {
        var limit = CGFloat.greatestFiniteMagnitude
        let room = 0.5 - inset
        for point in corners {
            for offset in [abs(point.x - 0.5), abs(point.y - 0.5)] where offset > 0.0001 {
                limit = min(limit, room / offset)
            }
        }
        return limit
    }

    static func flat(_ corners: [CGPoint]) -> [Double] { corners.flatMap { [Double($0.x), Double($0.y)] } }

    static func points(_ values: [Double]) -> [CGPoint] {
        stride(from: 0, to: values.count - 1, by: 2).map { CGPoint(x: values[$0], y: values[$0 + 1]) }
    }
}

// MARK: - One Euro filter

/// The One Euro filter (Casiez, Roussel and Vogel, CHI 2012) on a vector of
/// values: a low-pass filter whose cutoff rises with speed, so a still signal
/// is smoothed heavily and a moving one follows with little lag. It also
/// keeps a filtered velocity, which the bracket motion uses to project ahead.
public struct CKeyOneEuroFilter: Sendable {
    /// Cutoff when still, in Hz: lower is steadier, higher follows faster.
    public var minCutoff: Double
    /// Cutoff gain per unit of speed (units per second).
    public var beta: Double
    /// Cutoff of the velocity estimate, in Hz.
    public var derivativeCutoff: Double

    /// The filtered values; `nil` before the first sample.
    public private(set) var value: [Double]?
    /// The filtered velocity, in units per second.
    public private(set) var velocity: [Double] = []
    /// The time of the last sample.
    public private(set) var time: TimeInterval = 0

    public init(minCutoff: Double = 1.2, beta: Double = 0.02, derivativeCutoff: Double = 6) {
        self.minCutoff = minCutoff
        self.beta = beta
        self.derivativeCutoff = derivativeCutoff
    }

    /// The smoothing factor of a first-order low-pass at `cutoff` Hz over `dt` seconds.
    public static func alpha(cutoff: Double, dt: Double) -> Double {
        let tau = 1 / (2 * Double.pi * max(cutoff, 1e-6))
        return 1 / (1 + tau / max(dt, 1e-9))
    }

    /// Feeds a sample taken at `time` (seconds). The first sample, or one of
    /// a different length, is adopted as is. Returns the filtered values.
    @discardableResult
    public mutating func filter(_ sample: [Double], at time: TimeInterval) -> [Double] {
        guard let previous = value, previous.count == sample.count, velocity.count == sample.count else {
            value = sample
            velocity = [Double](repeating: 0, count: sample.count)
            self.time = time
            return sample
        }
        // Uneven delivery is not motion: bound the step both ways.
        let dt = max(1.0 / 240, min(0.25, time - self.time))
        var nextValue = previous
        var nextVelocity = velocity
        let derivativeAlpha = Self.alpha(cutoff: derivativeCutoff, dt: dt)
        for index in sample.indices {
            let rawVelocity = (sample[index] - previous[index]) / dt
            let filteredVelocity = velocity[index] + derivativeAlpha * (rawVelocity - velocity[index])
            let cutoff = minCutoff + beta * abs(filteredVelocity)
            nextValue[index] = previous[index] + Self.alpha(cutoff: cutoff, dt: dt) * (sample[index] - previous[index])
            nextVelocity[index] = filteredVelocity
        }
        value = nextValue
        velocity = nextVelocity
        self.time = time
        return nextValue
    }

    /// Forgets everything; the next sample is adopted as is.
    public mutating func reset() {
        value = nil
        velocity = []
        time = 0
    }
}

// MARK: - Critically damped spring

/// A critically damped spring (damping ratio 1: no bounce) on a vector of
/// values, integrated with the exact solution of each step, so it is stable
/// at any display rate and carries its velocity from one target to the next.
public struct CKeyCriticalSpring: Sendable {
    /// The time scale of the motion in seconds; smaller is stiffer.
    public var response: Double
    /// The current values; `nil` until `snap(to:)` or the first `step`.
    public private(set) var position: [Double]?
    /// The current velocity, in units per second.
    public private(set) var velocity: [Double] = []

    public init(response: Double = 0.08) {
        self.response = response
    }

    /// Jumps to `target` at rest.
    public mutating func snap(to target: [Double]) {
        position = target
        velocity = [Double](repeating: 0, count: target.count)
    }

    /// Advances `dt` seconds towards `target` and returns the new values. The
    /// first step, or a target of a different length, snaps.
    @discardableResult
    public mutating func step(towards target: [Double], dt: Double) -> [Double] {
        guard var x = position, x.count == target.count, velocity.count == target.count else {
            snap(to: target)
            return target
        }
        guard dt > 0, response > 0 else { return x }
        var v = velocity
        let omega = 2 * Double.pi / response
        let decay = exp(-omega * dt)
        for index in target.indices {
            // x(t) = goal + (e + (v + omega e) t) exp(-omega t), with e = x - goal.
            let e = x[index] - target[index]
            let k = v[index] + omega * e
            x[index] = target[index] + (e + k * dt) * decay
            v[index] = (v[index] - omega * k * dt) * decay
        }
        position = x
        velocity = v
        return x
    }

    /// Forgets the position; the next step snaps.
    public mutating func reset() {
        position = nil
        velocity = []
    }
}

// MARK: - Bracket motion

/// How the built-in brackets move, in three stages:
///
/// 1. **One Euro filter** on the eight corner coordinates, once per new
///    measurement and timed by the camera frame's *capture* time, so uneven
///    delivery is not mistaken for motion.
/// 2. **Latency projection.** The filtered target is projected along its
///    filtered velocity by `leadGain` of the time from the frame's capture to
///    the moment this display frame reaches the screen, capped at `maxLead`.
/// 3. **A critically damped spring**, integrated exactly on every display
///    refresh: the brackets carry velocity and never restart on a new
///    measurement.
///
/// Units are whatever the corners are in (screen points in the overlay);
/// times are host seconds. Use it directly to animate a custom overlay.
public struct CKeyBracketMotion: Sendable {
    /// One Euro cutoff when still, in Hz (default 1.2).
    public var minCutoff: Double {
        get { filter.minCutoff }
        set { filter.minCutoff = newValue }
    }
    /// One Euro cutoff gain per point per second of speed (default 0.02).
    public var beta: Double {
        get { filter.beta }
        set { filter.beta = newValue }
    }
    /// One Euro derivative cutoff, in Hz (default 6).
    public var derivativeCutoff: Double {
        get { filter.derivativeCutoff }
        set { filter.derivativeCutoff = newValue }
    }
    /// Spring response in seconds (default 0.08).
    public var response: Double {
        get { spring.response }
        set { spring.response = newValue }
    }
    /// Fraction of the measured latency to project (default 0.75; a little
    /// under the full latency keeps sudden stops from overshooting).
    public var leadGain: Double = 0.75
    /// The most latency the projection may cover, in seconds (default 0.12).
    public var maxLead: Double = 0.12

    private var filter = CKeyOneEuroFilter()
    private var spring = CKeyCriticalSpring()
    private var measuredAt: TimeInterval?
    private var lastStep: TimeInterval?

    public init() {}

    /// Advances to the display frame shown at `displayTime`.
    /// - Parameters:
    ///   - target: the latest measured corners (four points), or `nil` when
    ///     there is no track; `nil` resets the motion.
    ///   - captureTime: when `target` was captured. The filter runs once per
    ///     distinct capture time.
    ///   - displayTime: when this display frame reaches the screen
    ///     (`CADisplayLink.targetTimestamp`).
    /// - Returns: the corners to draw, or `nil` when there is nothing to draw.
    public mutating func step(towards target: [CGPoint]?, capturedAt captureTime: TimeInterval,
                              displayAt displayTime: TimeInterval) -> [CGPoint]? {
        guard let target, target.count == 4 else {
            reset()
            return nil
        }
        // 1. One Euro filter, once per new measurement. Match the new corners
        // to the ones being drawn first, so a re-seeded track cannot twist.
        if measuredAt != captureTime {
            let aligned = filter.value.map { CKeyCorners.matched(target, to: CKeyCorners.points($0)) } ?? target
            filter.filter(CKeyCorners.flat(aligned), at: captureTime)
            measuredAt = captureTime
        }
        guard let base = filter.value, base.count == 8 else { return target }
        let velocity = filter.velocity

        // 2. Project from the capture time to the display time.
        let lead = max(0, min(maxLead, displayTime - captureTime)) * leadGain
        let goal = (0..<8).map { base[$0] + velocity[$0] * lead }

        // 3. Critically damped spring towards the goal (exact solution per step).
        guard let previous = lastStep, spring.position != nil else {
            spring.snap(to: goal)  // first sight: snap onto the code
            lastStep = displayTime
            return CKeyCorners.points(goal)
        }
        let dt = max(0, min(0.1, displayTime - previous))
        lastStep = displayTime
        return CKeyCorners.points(spring.step(towards: goal, dt: dt))
    }

    /// Forgets the track; the next target snaps into place.
    public mutating func reset() {
        filter.reset()
        spring.reset()
        measuredAt = nil
        lastStep = nil
    }
}

// MARK: - C array helpers

typealias CKeyQuadTuple = (Double, Double, Double, Double, Double, Double, Double, Double)

func ckeyQuadArray(_ quad: CKeyQuadTuple) -> [Double] {
    [quad.0, quad.1, quad.2, quad.3, quad.4, quad.5, quad.6, quad.7]
}

func ckeyQuadTuple(_ quad: [Double]) -> CKeyQuadTuple {
    (quad[0], quad[1], quad[2], quad[3], quad[4], quad[5], quad[6], quad[7])
}
