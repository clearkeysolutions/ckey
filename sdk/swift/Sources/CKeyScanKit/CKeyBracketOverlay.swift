#if canImport(UIKit) && canImport(AVFoundation) && os(iOS)
import AVFoundation
import QuartzCore
import UIKit
#if canImport(SwiftUI)
import SwiftUI
#endif

/// The built-in brackets that snap onto a tracked code: one bracket at each
/// of its four corners, leaning with a tilted code. It is opt-in. An app that
/// draws its own overlay uses `CKeyCameraScanner.onTrack` and never adds this
/// view.
///
/// Lay it exactly over the camera preview and connect three things:
///
///     let feed = CKeyTrackFeed()
///     scanner.onTrack = { feed.publish($0) }      // any queue
///     overlay.feed = feed
///     overlay.previewLayer = previewView.previewLayer
///
/// The brackets are `CAShapeLayer`s stepped by a `CADisplayLink` on every
/// screen refresh (60 to 120 Hz), so their motion never waits on a view
/// update; the corners arrive through the lock-protected feed. Motion is
/// smoothed in screen space by `CKeyBracketMotion`: a One Euro filter timed
/// by capture time, a projection over the measured latency, and a critically
/// damped spring. For 120 Hz on iPhone the app's Info.plist needs
/// `CADisableMinimumFrameDurationOnPhone`.
open class CKeyBracketOverlay: UIView {
    // MARK: Input

    /// The latest track; the display link reads it on every refresh.
    public var feed: CKeyTrackFeed?
    /// The preview layer under this view, used to convert the track's sensor
    /// coordinates to view points. This view's bounds must match the layer's.
    public weak var previewLayer: AVCaptureVideoPreviewLayer?
    /// An alternative to `previewLayer`: a geometry that
    /// `CKeyCameraPreview(session:geometry:)` fills in.
    public var geometry: CKeyPreviewGeometry?

    // MARK: Motion (see `CKeyBracketMotion`)

    /// One Euro cutoff when still, in Hz: lower is steadier (default 1.2).
    public var minCutoff: Double = 1.2 { didSet { motion.minCutoff = minCutoff } }
    /// One Euro cutoff gain per point per second: higher follows fast motion
    /// more closely (default 0.02).
    public var beta: Double = 0.02 { didSet { motion.beta = beta } }
    /// Spring response in seconds: lower is snappier (default 0.08).
    public var response: Double = 0.08 { didSet { motion.response = response } }
    /// Fraction of the camera-to-screen latency the brackets are projected
    /// ahead by (default 0.75; 0 turns the projection off).
    public var leadGain: Double = 0.75 { didSet { motion.leadGain = leadGain } }

    // MARK: Style

    /// The bracket colour (default white).
    public var color: UIColor = .white { didSet { applyColours() } }
    /// The halo drawn under the brackets, so they show on any image
    /// (default black at 35%).
    public var haloColor: UIColor = UIColor.black.withAlphaComponent(0.35) { didSet { applyColours() } }
    /// The bracket colour while highlighted (default system green).
    public var highlightColor: UIColor = .systemGreen { didSet { applyColours() } }
    /// The halo colour while highlighted (default a darker green).
    public var highlightHaloColor: UIColor = UIColor(red: 0.05, green: 0.35, blue: 0.2, alpha: 1) {
        didSet { applyColours() }
    }
    /// Draws the highlight colours: set it when a code was read.
    public var isHighlighted = false { didSet { if isHighlighted != oldValue { applyColours() } } }
    /// Highlights by itself once the tracked code has decoded (default on).
    /// Turn it off to drive `isHighlighted` yourself.
    public var highlightsDecoded = true
    /// Bracket stroke width in points (default 4).
    public var lineWidth: CGFloat = 4 { didSet { stroke.lineWidth = lineWidth } }
    /// Halo stroke width in points (default 8).
    public var haloWidth: CGFloat = 8 { didSet { halo.lineWidth = haloWidth } }
    /// Length of each bracket arm as a fraction of the code's shortest edge
    /// (default 1/6).
    public var cornerLength: CGFloat = 1.0 / 6
    /// The shortest a bracket arm gets, in points (default 8).
    public var minimumCornerLength: CGFloat = 8
    /// How far outside the code the brackets sit, as a fraction of its size
    /// (default 0.10).
    public var padding: CGFloat = 0.10
    /// The smallest the brackets get, corner to opposite corner, in points
    /// (default 64), so they stay visible on a tiny code.
    public var minimumSize: CGFloat = 64

    // MARK: State

    /// True while the brackets are drawn on a code.
    public private(set) var isShowing = false
    /// Called on the main thread when the brackets appear on or leave a code
    /// (for example to hide a resting reticle).
    public var onShowingChange: ((Bool) -> Void)?

    private let halo = CAShapeLayer()
    private let stroke = CAShapeLayer()
    private var link: CADisplayLink?
    private var motion = CKeyBracketMotion()
    private var decodedHighlight = false

    public override init(frame: CGRect) {
        super.init(frame: frame)
        setUp()
    }

    public required init?(coder: NSCoder) {
        super.init(coder: coder)
        setUp()
    }

    private func setUp() {
        isUserInteractionEnabled = false
        backgroundColor = .clear
        isOpaque = false
        for shape in [halo, stroke] {
            shape.fillColor = nil
            shape.lineCap = .round
            shape.lineJoin = .round
            shape.isHidden = true
            // No implicit animations: the display link is the animation.
            shape.actions = ["path": NSNull(), "strokeColor": NSNull(), "hidden": NSNull(), "lineWidth": NSNull()]
            layer.addSublayer(shape)
        }
        halo.lineWidth = haloWidth
        stroke.lineWidth = lineWidth
        applyColours()
    }

    private func applyColours() {
        let highlighted = isHighlighted || decodedHighlight
        stroke.strokeColor = (highlighted ? highlightColor : color).cgColor
        halo.strokeColor = (highlighted ? highlightHaloColor : haloColor).cgColor
    }

    open override func layoutSubviews() {
        super.layoutSubviews()
        halo.frame = bounds
        stroke.frame = bounds
    }

    open override func didMoveToWindow() {
        super.didMoveToWindow()
        link?.invalidate()
        link = nil
        guard window != nil else {
            hide()
            motion.reset()
            return
        }
        let link = CADisplayLink(target: self, selector: #selector(tick(_:)))
        link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 120, preferred: 120)
        link.add(to: .main, forMode: .common)
        self.link = link
    }

    deinit { link?.invalidate() }

    private func hide() {
        if !stroke.isHidden {
            stroke.isHidden = true
            halo.isHidden = true
        }
        if isShowing {
            isShowing = false
            onShowingChange?(false)
        }
    }

    private func viewPoints(_ corners: [CGPoint]) -> [CGPoint]? {
        if let layer = previewLayer {
            return CKeyPreviewGeometry.viewPoints(fromSensorPoints: corners, in: layer)
        }
        return geometry?.viewPoints(fromSensorPoints: corners)
    }

    @objc private func tick(_ link: CADisplayLink) {
        // Where the code is: its latest measurement in view points, with the
        // frame's capture time, and the time this display frame reaches the screen.
        var target: [CGPoint]?
        var capturedAt: TimeInterval = 0
        var decoded = false
        if let track = feed?.current, let points = viewPoints(track.corners),
           bounds.contains(CKeyCorners.centroid(points)),
           let grown = CKeyCorners.expanded(points, padding: padding, minimumSpan: minimumSize / 2) {
            target = grown
            capturedAt = track.captureTime
            decoded = track.decoded
        }
        guard let shown = motion.step(towards: target, capturedAt: capturedAt, displayAt: link.targetTimestamp) else {
            hide()
            return
        }
        let highlight = highlightsDecoded && decoded
        if highlight != decodedHighlight {
            decodedHighlight = highlight
            applyColours()
        }
        let path = bracketPath(shown)
        halo.path = path
        stroke.path = path
        if stroke.isHidden {
            stroke.isHidden = false
            halo.isHidden = false
        }
        if !isShowing {
            isShowing = true
            onShowingChange?(true)
        }
    }

    /// Corner brackets on a quadrilateral: at each corner, two strokes along
    /// its two edges.
    private func bracketPath(_ corners: [CGPoint]) -> CGPath {
        let path = CGMutablePath()
        guard corners.count == 4 else { return path }
        var shortest = CGFloat.greatestFiniteMagnitude
        for index in 0..<4 {
            let a = corners[index], b = corners[(index + 1) % 4]
            shortest = min(shortest, hypot(b.x - a.x, b.y - a.y))
        }
        let length = max(minimumCornerLength, shortest * cornerLength)
        for index in 0..<4 {
            let corner = corners[index], previous = corners[(index + 3) % 4], next = corners[(index + 1) % 4]
            path.move(to: CKeyBracketOverlay.point(from: corner, toward: previous, by: length))
            path.addLine(to: corner)
            path.addLine(to: CKeyBracketOverlay.point(from: corner, toward: next, by: length))
        }
        return path
    }

    private static func point(from corner: CGPoint, toward other: CGPoint, by length: CGFloat) -> CGPoint {
        let distance = hypot(other.x - corner.x, other.y - corner.y)
        guard distance > 0 else { return corner }
        let t = min(1, length / distance)
        return CGPoint(x: corner.x + (other.x - corner.x) * t, y: corner.y + (other.y - corner.y) * t)
    }
}

#if canImport(SwiftUI)
/// `CKeyBracketOverlay` for SwiftUI. Put it over the camera preview in a
/// `ZStack`, sharing a `CKeyPreviewGeometry` with the preview:
///
///     @StateObject private var geometry = CKeyPreviewGeometry()
///     let feed = CKeyTrackFeed()               // scanner.onTrack = { feed.publish($0) }
///     ZStack {
///         CKeyCameraPreview(session: scanner.session, geometry: geometry)
///         CKeyBracketOverlayView(feed: feed, geometry: geometry)
///             .allowsHitTesting(false)
///     }
///
/// The corners never pass through SwiftUI state: the view reads the feed on
/// every display refresh.
public struct CKeyBracketOverlayView: UIViewRepresentable {
    private let feed: CKeyTrackFeed
    private let geometry: CKeyPreviewGeometry
    private let isHighlighted: Bool
    private let configure: ((CKeyBracketOverlay) -> Void)?

    /// - Parameters:
    ///   - feed: the track feed the scanner's `onTrack` publishes to.
    ///   - geometry: the geometry of the preview under this view.
    ///   - isHighlighted: draw the highlight colours (a code was read).
    ///   - configure: sets tunables and styling on the underlying view
    ///     (`minCutoff`, `beta`, `response`, `leadGain`, colours, widths,
    ///     `cornerLength`, `padding`); called on every SwiftUI update.
    public init(feed: CKeyTrackFeed, geometry: CKeyPreviewGeometry, isHighlighted: Bool = false,
                configure: ((CKeyBracketOverlay) -> Void)? = nil) {
        self.feed = feed
        self.geometry = geometry
        self.isHighlighted = isHighlighted
        self.configure = configure
    }

    public func makeUIView(context: Context) -> CKeyBracketOverlay {
        let view = CKeyBracketOverlay()
        apply(to: view)
        return view
    }

    public func updateUIView(_ view: CKeyBracketOverlay, context: Context) {
        apply(to: view)
    }

    private func apply(to view: CKeyBracketOverlay) {
        view.feed = feed
        view.geometry = geometry
        view.isHighlighted = isHighlighted
        configure?(view)
    }
}
#endif
#endif
