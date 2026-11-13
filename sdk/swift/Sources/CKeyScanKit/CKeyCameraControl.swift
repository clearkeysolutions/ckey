#if canImport(AVFoundation) && (os(iOS) || os(macOS))
import AVFoundation
import CoreGraphics
import Foundation
import QuartzCore

/// Drives one `AVCaptureDevice` for scanning: focus, exposure and zoom, from
/// the core's camera advice. `CKeyCameraScanner` uses one for the camera it
/// owns. A host that owns its capture session ("bring your own camera") can
/// create one for its device and feed it the advice of a `CKeyFrameProcessor`:
///
///     let control = CKeyCameraControl(device: device)
///     control.prepare()                                  // once the session has its final format
///     processor.onAdvice = { control.apply($0) }
///     control.onZoomChange = { processor.zoomDidChange(by: $0) }
///
/// Turn off what the host manages itself (`focusOnCode`, `exposureControl`,
/// `realDetailZoom`), so the two do not fight over the device.
///
/// What it does:
///
/// - **Focus**: continuous autofocus, restricted to the near range where the
///   device supports it, aimed at the code (or at `restingPoint` without
///   one) and re-aimed only when the core says the point moved.
/// - **Exposure**: continuous auto exposure metered on the same point, with
///   the core's exposure bias (negative when the code clips, and cycling
///   0 / -1 / -2 EV while nothing decodes), clamped to the device's range.
/// - **Zoom**: only where it adds real detail. Up to the active format's
///   `videoZoomFactorUpscaleThreshold` the sensor is cropped at full
///   resolution; past it the image is only enlarged, which is not used. On a
///   virtual multi-camera device a far code may also switch to the telephoto
///   lens, at exactly its switch-over factor.
/// - **Lens**: on a virtual device with an ultra-wide lens the zoom never
///   goes below the wide lens's field of view, so the ultra-wide is used only
///   when iOS switches to it for close focus (macro).
///
/// All device work happens on a private serial queue; every method returns
/// at once and may be called from any thread. On macOS only the focus and
/// exposure points apply.
public final class CKeyCameraControl {
    /// The longest exposure the control allows, as 1/n s (with `exposureControl`).
    public static let maxExposureTimescale: Int32 = 120
    /// The device this control drives.
    public let device: AVCaptureDevice

    /// Aim continuous autofocus at the code (default on).
    public var focusOnCode: Bool {
        get { sync { flags.focusOnCode } }
        set { sync { flags.focusOnCode = newValue } }
    }
    /// Meter exposure on the code and apply the core's exposure bias (default on).
    public var exposureControl: Bool {
        get { sync { flags.exposureControl } }
        set { sync { flags.exposureControl = newValue } }
    }
    /// Zoom on a small code, only as far as the sensor gives real detail (default on).
    public var realDetailZoom: Bool {
        get { sync { flags.realDetailZoom } }
        set { sync { flags.realDetailZoom = newValue } }
    }
    /// Where the lens cannot focus on a code of `typicalCodeWidth` held to
    /// fill `typicalFill` of the frame, zoom so the user holds the phone at
    /// or beyond `minimumFocusDistance` (Apple's barcode guidance; default
    /// on). This zoom is at most 2x and may go past real detail: it trades
    /// nothing in resolution for a code that is in focus.
    public var closeFocusZoom: Bool {
        get { sync { flags.closeFocusZoom } }
        set { sync { flags.closeFocusZoom = newValue } }
    }
    /// Enable the device's geometric distortion correction where supported
    /// (default on): the reader's fit is a planar one.
    public var distortionCorrection: Bool {
        get { sync { flags.distortionCorrection } }
        set { sync { flags.distortionCorrection = newValue } }
    }
    /// Where to focus and meter while no code is in view, in capture-device
    /// coordinates (default the frame centre): the reticle's centre.
    public var restingPoint: CGPoint {
        get { sync { flags.restingPoint } }
        set { sync { flags.restingPoint = newValue } }
    }
    /// The width in millimetres of a typical code, for `closeFocusZoom` (default 50).
    public var typicalCodeWidth: Double {
        get { sync { flags.typicalCodeWidth } }
        set { sync { flags.typicalCodeWidth = newValue } }
    }
    /// The share of the frame's shorter side a typical code fills, for
    /// `closeFocusZoom` (default 0.6).
    public var typicalFill: Double {
        get { sync { flags.typicalFill } }
        set { sync { flags.typicalFill = newValue } }
    }
    /// Called on the control's queue after the zoom changed, with the new
    /// zoom divided by the old. Pass it to `CKeyFrameProcessor.zoomDidChange(by:)`.
    public var onZoomChange: ((Double) -> Void)? {
        get { sync { flags.onZoomChange } }
        set { sync { flags.onZoomChange = newValue } }
    }

    private struct Flags {
        var focusOnCode = true
        var exposureControl = true
        var realDetailZoom = true
        var closeFocusZoom = true
        var distortionCorrection = true
        var restingPoint = CGPoint(x: 0.5, y: 0.5)
        var typicalCodeWidth = 50.0
        var typicalFill = 0.6
        var onZoomChange: ((Double) -> Void)?
    }

    private let lock = NSLock()
    private var flags = Flags()
    private let queue = DispatchQueue(label: "org.openckey.scanner.camera", qos: .userInitiated)

    // Queue-only state.
    private var baseZoom: CGFloat = 1
    private var aimed = false
    private var lastBias: Float?
    private var lastRefocus: CFTimeInterval = 0
    private var lastCandidate: CFTimeInterval = 0
    /// Frames still in flight after a zoom change were taken at the old zoom:
    /// zoom advice is ignored until this time.
    private var settledAt: CFTimeInterval = 0
    /// Zoom-ins since the zoom was last at its base: one snap and at most one
    /// correction per code, so the view does not breathe.
    private var snaps = 0

    /// After a zoom change, frames take about this long to arrive at the new zoom.
    private static let settleTime: CFTimeInterval = 0.35
    /// Back to the base zoom after this long without a candidate.
    private static let releaseAfter: CFTimeInterval = 2.5
    /// Autofocus is restarted for "out of focus" advice at most this often.
    private static let refocusInterval: CFTimeInterval = 1.5

    public init(device: AVCaptureDevice) {
        self.device = device
    }

    @discardableResult
    private func sync<T>(_ body: () -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body()
    }

    // MARK: Device choice

    /// The camera to scan with: the back virtual multi-camera device where
    /// there is one (`.builtInTripleCamera`, then `.builtInDualWideCamera`),
    /// else the wide camera. A virtual device switches to its ultra-wide
    /// lens for close focus by itself; the ultra-wide alone is never chosen,
    /// because its distortion defeats the reader. On macOS, the default camera.
    public static func preferredDevice() -> AVCaptureDevice? {
        #if os(iOS)
        let types: [AVCaptureDevice.DeviceType] = [.builtInTripleCamera, .builtInDualWideCamera, .builtInWideAngleCamera]
        let found = AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: .back).devices
        for type in types {
            if let device = found.first(where: { $0.deviceType == type }) { return device }
        }
        return AVCaptureDevice.default(for: .video)
        #else
        return AVCaptureDevice.default(for: .video)
        #endif
    }

    // MARK: Driving the device

    /// Configures the device for scanning. Call it once the device is in a
    /// session whose preset or format is final (zoom limits depend on the
    /// active format), and again after changing the format.
    public func prepare() {
        queue.async { self.configure() }
    }

    /// Applies one frame's advice. Cheap when nothing changes.
    public func apply(_ advice: CKeyCameraAdvice) {
        queue.async { self.applyOnQueue(advice) }
    }

    /// Aims focus and exposure at `restingPoint` now (for example after the
    /// reticle moved).
    public func aimAtRest() {
        queue.async {
            let flags = self.sync { self.flags }
            self.aim(at: flags.restingPoint, focus: flags.focusOnCode, exposure: flags.exposureControl)
        }
    }

    /// Back to the resting state: base zoom, no exposure bias, aimed at
    /// `restingPoint`. Call it when a new scan starts.
    public func reset() {
        queue.async {
            let flags = self.sync { self.flags }
            self.snaps = 0
            self.lastCandidate = 0
            self.setBias(0, enabled: flags.exposureControl)
            #if os(iOS)
            if flags.realDetailZoom, abs(self.device.videoZoomFactor - self.baseZoom) > 0.001 {
                self.setZoom(self.baseZoom)
            }
            #endif
            self.aim(at: flags.restingPoint, focus: flags.focusOnCode, exposure: flags.exposureControl)
        }
    }

    /// The highest zoom factor used: only real detail (see the class notes).
    /// Meaningful once `prepare()` has run. It waits for the control's queue:
    /// do not read it from `onZoomChange`.
    public var detailZoomLimit: CGFloat {
        queue.sync { self.detailLimit() }
    }

    // MARK: Queue work

    private func configure() {
        let flags = sync { self.flags }
        do {
            try device.lockForConfiguration()
        } catch {
            return
        }
        if device.isFocusModeSupported(.continuousAutoFocus) { device.focusMode = .continuousAutoFocus }
        if device.isExposureModeSupported(.continuousAutoExposure) { device.exposureMode = .continuousAutoExposure }
        #if os(iOS)
        // Short exposures: auto-exposure may otherwise pick 1/30 s indoors,
        // which smears a handheld code across several pixels. The cap trades
        // a little noise (higher ISO) for sharp frames.
        if flags.exposureControl {
            let cap = CMTime(value: 1, timescale: Self.maxExposureTimescale)
            let format = device.activeFormat
            if CMTimeCompare(format.maxExposureDuration, cap) > 0 {
                device.activeMaxExposureDuration = CMTimeMaximum(format.minExposureDuration, cap)
            }
        }
        if flags.focusOnCode, device.isAutoFocusRangeRestrictionSupported {
            device.autoFocusRangeRestriction = .near
        }
        // The point of interest is set by this control, not by scene changes.
        device.isSubjectAreaChangeMonitoringEnabled = false
        if flags.distortionCorrection, device.isGeometricDistortionCorrectionSupported {
            device.isGeometricDistortionCorrectionEnabled = true
        }
        baseZoom = restingZoom(flags)
        let zoom = min(max(baseZoom, device.minAvailableVideoZoomFactor), device.maxAvailableVideoZoomFactor)
        baseZoom = zoom
        if abs(device.videoZoomFactor - zoom) > 0.001 { device.videoZoomFactor = zoom }
        #endif
        device.unlockForConfiguration()
        snaps = 0
        lastBias = nil
        aimed = false
        aim(at: flags.restingPoint, focus: flags.focusOnCode, exposure: flags.exposureControl)
    }

    #if os(iOS)
    /// The zoom to rest at: the wide lens's field of view on a virtual device
    /// that starts at the ultra-wide, times the close-focus zoom if needed.
    private func restingZoom(_ flags: Flags) -> CGFloat {
        var zoom: CGFloat = 1
        if device.deviceType == .builtInTripleCamera || device.deviceType == .builtInDualWideCamera,
           let wide = device.virtualDeviceSwitchOverVideoZoomFactors.first {
            zoom = max(1, CGFloat(truncating: wide))
        }
        guard flags.closeFocusZoom, flags.typicalCodeWidth > 0, flags.typicalFill > 0 else { return zoom }
        let nearest = device.minimumFocusDistance  // millimetres; -1 when unknown
        guard nearest > 0 else { return zoom }
        // The field of view at `zoom`: videoFieldOfView is the horizontal angle at 1x.
        let halfAngle = Double(device.activeFormat.videoFieldOfView) * Double.pi / 360
        let halfTangent = tan(halfAngle) / Double(zoom)
        guard halfTangent.isFinite, halfTangent > 0 else { return zoom }
        // A code filling `typicalFill` of the shorter side fills this much of the longer one.
        let dimensions = CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription)
        let longer = Double(max(dimensions.width, dimensions.height)), shorter = Double(min(dimensions.width, dimensions.height))
        guard longer > 0, shorter > 0 else { return zoom }
        let fill = min(1, flags.typicalFill * shorter / longer)
        // The distance at which the code fills that share of the frame.
        let distance = flags.typicalCodeWidth / fill / (2 * halfTangent)
        guard distance.isFinite, distance > 0, distance < Double(nearest) else { return zoom }
        return zoom * CGFloat(min(2, Double(nearest) / distance))
    }
    #endif

    private func detailLimit() -> CGFloat {
        #if os(iOS)
        let threshold = device.activeFormat.videoZoomFactorUpscaleThreshold
        return max(baseZoom, min(max(threshold, 1), device.maxAvailableVideoZoomFactor))
        #else
        return 1
        #endif
    }

    private func applyOnQueue(_ advice: CKeyCameraAdvice) {
        let flags = sync { self.flags }
        let now = CACurrentMediaTime()
        let point = advice.hasCandidate ? advice.point : flags.restingPoint

        // Focus and exposure follow the point, re-aimed only when it moved
        // (so autofocus does not keep restarting) or the code is out of focus.
        var refocus = false
        if advice.guidance.contains(.refocus), now - lastRefocus > Self.refocusInterval {
            refocus = true
            lastRefocus = now
        }
        if advice.reaim || !aimed {
            aim(at: point, focus: flags.focusOnCode, exposure: flags.exposureControl)
        } else if refocus {
            aim(at: point, focus: flags.focusOnCode, exposure: false)
        }
        setBias(advice.exposureBiasEV, enabled: flags.exposureControl)

        #if os(iOS)
        guard flags.realDetailZoom else { return }
        guard advice.hasCandidate else {
            // Nothing in view for a while: back to the base zoom, to look everywhere again.
            if lastCandidate > 0, now - lastCandidate > Self.releaseAfter {
                lastCandidate = 0
                snaps = 0
                if abs(device.videoZoomFactor - baseZoom) > 0.001 { setZoom(baseZoom) }
            }
            return
        }
        lastCandidate = now
        guard now >= settledAt, advice.zoomFactor.isFinite, advice.zoomFactor > 0 else { return }
        let current = device.videoZoomFactor
        guard current > 0 else { return }
        var desired = current * CGFloat(advice.zoomFactor)
        if advice.zoomFactor > 1 {
            // The zoom is centred on the frame, so every point moves away from
            // the centre: keep the whole code in view.
            let keep = CKeyCorners.largestZoomKeepingInFrame(advice.corners)
            desired = min(desired, current * max(1, keep))
        }
        desired = realDetail(desired)
        let change = abs(desired - current) / current
        if desired > current {
            // Hold steady: the first snap needs a 12% change; after it, one
            // correction only, and only for a 25% change.
            // (Two conditions, not one `snaps < 2, change > (...)`: Swift
            // parses that as a generic specialization `snaps<2, change>`.)
            let needed: CGFloat = snaps == 0 ? 0.12 : 0.25
            guard snaps < 2 else { return }
            guard change > needed else { return }
            snaps += 1
        } else {
            guard change > 0.1 else { return }
            if abs(desired - baseZoom) < 0.001 { snaps = 0 }
        }
        setZoom(desired)
        #endif
    }

    #if os(iOS)
    /// `zoom` limited to real detail: the base zoom up to the upscale
    /// threshold, or exactly a telephoto lens's switch-over factor when the
    /// request reaches it.
    private func realDetail(_ zoom: CGFloat) -> CGFloat {
        var result = min(max(zoom, baseZoom), detailLimit())
        let lenses = device.virtualDeviceSwitchOverVideoZoomFactors.map { CGFloat(truncating: $0) }
        if let lens = lenses.last(where: { $0 <= zoom }), lens > result { result = lens }
        return min(max(result, device.minAvailableVideoZoomFactor), device.maxAvailableVideoZoomFactor)
    }

    private func setZoom(_ factor: CGFloat) {
        let previous = device.videoZoomFactor
        let clamped = min(max(factor, device.minAvailableVideoZoomFactor), device.maxAvailableVideoZoomFactor)
        guard previous > 0, abs(clamped - previous) > 0.001 else { return }
        do {
            try device.lockForConfiguration()
        } catch {
            return
        }
        // Snap: set the zoom at once rather than ramping, then let in-flight frames pass.
        device.videoZoomFactor = clamped
        device.unlockForConfiguration()
        settledAt = CACurrentMediaTime() + Self.settleTime
        let handler = sync { flags.onZoomChange }
        handler?(Double(clamped / previous))
    }
    #endif

    private func aim(at point: CGPoint, focus: Bool, exposure: Bool) {
        guard focus || exposure else { return }
        let target = CGPoint(x: min(max(point.x, 0), 1), y: min(max(point.y, 0), 1))
        do {
            try device.lockForConfiguration()
        } catch {
            return
        }
        if focus, device.isFocusPointOfInterestSupported, device.isFocusModeSupported(.continuousAutoFocus) {
            device.focusPointOfInterest = target
            device.focusMode = .continuousAutoFocus  // setting the mode applies the point
        }
        if exposure, device.isExposurePointOfInterestSupported, device.isExposureModeSupported(.continuousAutoExposure) {
            device.exposurePointOfInterest = target
            device.exposureMode = .continuousAutoExposure
        }
        device.unlockForConfiguration()
        aimed = true
    }

    private func setBias(_ ev: Double, enabled: Bool) {
        #if os(iOS)
        guard enabled, ev.isFinite else { return }
        let bias = min(max(Float(ev), device.minExposureTargetBias), device.maxExposureTargetBias)
        if let last = lastBias, abs(last - bias) < 0.05 { return }
        do {
            try device.lockForConfiguration()
        } catch {
            return
        }
        device.setExposureTargetBias(bias, completionHandler: nil)
        device.unlockForConfiguration()
        lastBias = bias
        #endif
    }
}
#endif
