# CKey for Swift

A thin Swift package over the CKey C++20 core (iOS 15+, macOS 12+). It encodes and renders CKey symbols as native pixels (`CGImage`, SwiftUI, UIKit, AppKit), exports PNG, SVG and PDF, scans still images and live camera frames, and classifies decoded payloads. All encoding, decoding and rendering is done by the core in `src/`. The Swift layer reimplements none of it.

## Install

In Xcode, choose **File > Add Package Dependencies...** and enter the repository URL:

```
https://github.com/clearkeysolutions/ckey
```

Or add it to a `Package.swift`:

```swift
.package(url: "https://github.com/clearkeysolutions/ckey", from: "0.1.0"),
// ...
.target(name: "MyApp", dependencies: [.product(name: "CKey", package: "ckey")]),
```

`Package.swift` sits at the repository root because SwiftPM installs packages from the root of a Git repository. The Swift sources live in `sdk/swift/`.

### Products

| Product | Import | Contains |
|---|---|---|
| `CKey` | `import CKey` | Everything: encoding, the native renderer, views, scanning, payload helpers, style certification and the optional aggregate telemetry. `import CKey` also re-exports the scanning API. |
| `CKeyScanner` | `import CKeyScanKit` | Scanning only: `CKeyDecoder`, the camera layer (`CKeyCameraScanner`, `CKeyFrameProcessor`, `CKeyCameraControl`, `CKeyCameraPreview`, `CKeyBracketOverlay`) and `CKeyPayload`. It links the codec and scan libraries and nothing else, so it has no renderer, no telemetry and no networking. Use it in OS or vendor builds that must leave telemetry out entirely. |

Package layout: `CKeyCodec`, `CKeyScan`, `CKeyRender`, `CKeyCertify` and `CKeyTelemetry` are C++ targets. Each one compiles the matching file in `src/` in place, through a one-line shim under `sdk/swift/Sources/<Target>/`. `CKeyCodec` exposes `include/ckey/ckey.h` as a Clang module, and `CKeyTelemetry` does the same for `include/ckey/telemetry.h`. `CKeyScanKit` and `CKey` are the Swift API.

## Encode and render

```swift
import CKey

let symbol = try CKeySymbol.encode("https://openckey.org")          // version 0 = smallest that fits
let dense  = try CKeySymbol.encode(data, version: 12, level: .h, layout: .print)

let image: CGImage = try symbol.cgImage(size: 1024)                 // opaque, black on white
let overlay = try symbol.cgImage(size: 512, policy: .hybrid, transparent: true, style: .plain)
let png: Data  = try symbol.pngData(size: 1024, dpi: 300)
let svg: String = try symbol.svg(scale: 8, style: .concept)
let pdf: Data  = try symbol.pdfData(widthPoints: 144)                // 2 inches wide
let raw = try symbol.pixels(size: 256)                               // straight-alpha RGBA8 buffer

// Pixel-perfect (what CKeyView displays): every module a fixed block of whole pixels
let lattice = try symbol.lattice()                                   // cells: 0 background, 1 ink, 2+ accents
let exact: CGImage = try symbol.pixelPerfectImage(pxPerCell: lattice.pxPerCell(fitting: 900))
let exactPNG = try symbol.pixelPerfectPNG(pxPerCell: 4, dpi: 300)
let exactSVG = try symbol.pixelPerfectSVG(pxPerCell: 4)
```

`CKeyView`, `CKeyUIView` and `CKeyNSView` draw pixel-perfect. They show the core's lattice (`ckey_render_scalable_lattice`) with the most whole display pixels per cell that fit, unscaled and centred, so a code on screen is never resampled. `lattice(cellsPerModule:)` takes 0 (the default: 3 for `.print`, 6 for `.standard`, the smallest size that decoded at every version on clean output) or 1...6. Pixel-perfect output draws flat colours: no materials, and a logo is composited by the caller.

`size` is the pixel size of the full 31-unit canvas, which includes the quiet margin. The raster policies are `.coverage` (antialiased, the default), `.binary` and `.hybrid` (thin marks kept at least 1 px wide). `CKeySymbol.capacity(version:level:layout:)` gives the payload limit for a version. `CKeyError` explains any refusal.

### Correction levels and layouts

| `CKeyErrorCorrection` | Parity per block | Corrects (unknown bad bytes / known-missing bytes) |
|---|---|---|
| `.l` | 14% | about 7% / 14% |
| `.m` (default) | 28% | about 14% / 28% |
| `.q` | 44% | about 22% / 44% |
| `.h` | 60% | about 30% / 60% |
| `.u` (Ultra) | 80% | about 40% / 80% |
| `.x` (Extreme) | 95% | about 47.5% / 95% |

`.u` and `.x` spend most of the symbol on parity: `.u` carries about half of `.h`'s payload and `.x` about 5% of `.l`'s, and the smallest versions carry nothing at `.x` (`capacity` returns 0, and encoding throws). Readers before 0.1.0-build.86 reject both.

| `CKeyLayout` | Format | Use |
|---|---|---|
| `.standard` (default) | 0.27 | Screens and ordinary prints. |
| `.print` | 0.28 ("P0.1") | Small prints: larger marks and gaps, lower capacity per version. |
| `.standardGrouped` | 0.29 | The standard geometry with byte-grouped placement. |
| `.printGrouped` | 0.30 | The print geometry with byte-grouped placement. |

Byte-grouped placement puts each byte's eight bits on neighbouring sites, so a blot, glare spot or logo costs only the bytes under it. The geometry, marks, keys and capacity are those of the ungrouped layout. Readers before 0.1.0-build.86 cannot read grouped symbols, so the encoder emits them only when asked. The decoder identifies the layout itself and reports it in `CKeyResult.layout`; `layout.isPrint` and `layout.isGrouped` tell the families apart.

```swift
let tough = try CKeySymbol.encode("https://openckey.org", level: .u, layout: .standardGrouped)
```

### Styles

`CKeyStyle.plain` is black on white, and `CKeyStyle.concept` is cream metallic ink on a dark vignette. Every field of the core's `ckey_style` can be changed: colours, accents, module shape, ring style, ray caps, bevel, shadow, texture and logo.

```swift
var style = CKeyStyle.concept
style.modules = .circle
style.policy = .adaptive          // .strict (default) refuses unsafe styles; .override draws as given
try symbol.check(style: style)    // explains a refusal (contrast, effects, logo budget)
let report = try symbol.certify(style: style, transparent: true, backdrops: [.white, CKeyColor(hex: 0x202020)])
print(report.certified, report.message, report.adaptedStyle as Any)
```

Certification renders the symbol, composites it over the backdrops, applies a phone-camera model and decodes it with the real decoder. A transparent symbol with no backdrop is never certified. The model is a model and not a measurement, so real-camera tests remain the evidence.

## Views

```swift
import SwiftUI
import CKey

struct TicketView: View {
    var body: some View {
        CKeyView(data: "ABC123")
            .frame(width: 180, height: 180)
            .background(Color.white)                  // transparent by default: pick a contrasting backdrop
    }
}
```

`CKeyView` is square. It renders at its exact pixel size (points × display scale) through `CGImage`, so no SVG, file or web view is involved. It draws no background, border or chrome unless you pass `transparent: false`. It is one accessibility image with the label "CKey code"; change it with `accessibilityLabel:`. Other initialisers are `CKeyView(bytes:)` and `CKeyView(symbol:style:...)`.

UIKit and AppKit have `CKeyUIView` and `CKeyNSView`: set `symbol`, `style`, `transparent` and `policy`, or use `init(data:)`.

## Scan

### Still images and single frames

Headless, from any thread:

```swift
let result: CKeyResult? = CKeyDecoder.decode(cgImage: image)
let fromCamera = CKeyDecoder.decode(pixelBuffer: pixelBuffer)   // 420v/420f luma, gray8, BGRA/RGBA/ARGB
let fromPhoto = CKeyDecoder.decode(imageData: data)              // JPEG/HEIC/PNG: orientation applied, 4,096 then 2,048 px
if let result {
    result.bytes        // Data, verified by Reed-Solomon and CRC-32C
    result.text         // String? — UTF-8 without control characters (tab/LF/CR allowed)
    result.profile      // .scalable, .locator, .text, .extended
    result.version      // Profile 0 version 1...31 (0 otherwise)
    result.layout       // .standard / .print / .standardGrouped / .printGrouped for Profile 0
}
```

Every entry point is one call into the core's frame scan (`ckey_scan_frame_gray8`): it finds up to six border candidates, fits each one to the Profile 0 field and decodes it, then tries Profiles 1, 2 and 3, each refined against its own keys. Images are drawn over white, so a transparent rendering decodes as dark on white.

`CKeyDecoder.scan(...)` returns the whole outcome of that call, for callers that want more than the payload:

```swift
let outcome = CKeyDecoder.scan(gray: luma, width: w, height: h, stride: rowBytes,
                               region: CGRect(x: 600, y: 300, width: 720, height: 720))   // pixels; optional
outcome.result          // CKeyResult? — nil when nothing decoded
outcome.located         // a symbol-like candidate was found, decoded or not
outcome.quad            // its four corners, full-frame pixels (normalizedQuad: fractions of the frame)
outcome.score           // fit score against the CKey keys
outcome.symbolCut       // a symbol-like border runs off the frame or region: move back
outcome.erasuresUsed    // bytes read as erasures (glare, occlusion) in the decode that succeeded
outcome.correctedSymbols, outcome.candidates, outcome.budgetExhausted
```

**Built-in ML assistance** (on by default) helps only where the classical reader fails: the locator proposes the symbol after the border search finds nothing that decodes, the corner refiner corrects the corners after a failed decode, and the site model reads the modules after the plain reading fails. Frames the classical reader reads pay nothing for it. A payload is still accepted only after Reed-Solomon and CRC-32C. `CKeyDecoder.setMachineLearning(enabled: false)` makes scanning exactly classical for the whole process.

With a **region of interest** only that rectangle is searched, at full resolution and in place (no copy); coordinates in the outcome are always full-frame. A region around the expected symbol is faster and keeps the rest of the scene out of the search. Alternate it with whole frames so symbols outside the region still read. `decode(gray:width:height:stride:region:)` and `decode(pixelBuffer:normalizedRegion:)` take the same region; `CKeyDecoder.pixelRegion(normalized:width:height:)` converts a normalized rectangle (plus an 18% margin) to pixels.

### Live camera: the SDK owns the camera

`CKeyScanner` (in `CKey`, with telemetry sessions) or `CKeyCameraScanner(observer: nil)` (in `CKeyScanner`, without telemetry):

```swift
let scanner = CKeyScanner()
scanner.onResult = { result in            // main queue, first valid decode of the session
    show(result)                           // then call scanner.scanAgain() for the next one
}
scanner.onGuidance = { guidance in        // main queue; an empty set clears the hint
    hint = guidance.contains(.moveBack) ? "Move back, the code is cut off" : nil
}
scanner.start()                            // asks for camera access (NSCameraUsageDescription)
// SwiftUI: CKeyCameraPreview(session: scanner.session)
scanner.stop()
```

Call `start()`, `stop()` and `scanAgain()` from the main thread. No still capture is needed.

**The scanner takes control of the camera by default.** It chooses the capture device and drives focus, exposure and zoom from the core's per-frame camera advice (`ckey_camera_advise_gray8`), so every platform behaves the same. Each part is a flag, all on by default:

| Property | Default | What it does |
|---|---|---|
| `controlsCamera` | `true` | Master switch for `focusOnCode`, `exposureControl` and `realDetailZoom`. Off leaves the device alone after the initial setup. |
| `focusOnCode` | `true` | Continuous autofocus, restricted to the near range where supported, aimed at the reticle and then at the found code. It is re-aimed only when the code moves or is out of focus. |
| `exposureControl` | `true` | Exposure is metered on the code. The exposure bias goes negative when the code clips, and cycles 0 / −1 / −2 EV while nothing decodes (a screen too bright to detect comes into range on one of them). |
| `realDetailZoom` | `true` | Zooms on a small code only up to the active format's `videoZoomFactorUpscaleThreshold`, where the sensor is cropped at full resolution, or to a telephoto lens at its switch-over factor. Zoom that only enlarges pixels is not used. |
| `regionOfInterest` | `true` | Two of every three analysed frames decode only `scanRegion` plus an 18% margin; every third frame is decoded whole. |
| `multiFrame` | `true` | A frame that does not decode is averaged with the previous frames of the same symbol (in the symbol's own coordinates, so it may move), and the average is decoded. Frames that are each too noisy, glared or partly hidden can read together; `CKeyScanOutcome.accumulated` marks such a read. |
| `tracking` | `true` | Tracks the code's four corners on every frame and publishes them through `onTrack`. |
| `keepsScreenAwake` | `true` | Sets `UIApplication.isIdleTimerDisabled` while the camera runs, so the device does not auto-lock mid-scan. iOS apps only; it does nothing in app extensions. |

To opt out, turn off what your app manages itself, before `start()` or at any time:

```swift
scanner.controlsCamera = false      // the app drives focus, exposure and zoom
scanner.realDetailZoom = false      // or just one part
scanner.keepsScreenAwake = false    // the app manages the idle timer
```

Other things the scanner does:

- **Device choice.** `CKeyCameraControl.preferredDevice()` picks the back virtual multi-camera device (`.builtInTripleCamera`, then `.builtInDualWideCamera`) and falls back to `.builtInWideAngleCamera`. The zoom never goes below the wide lens's field of view, so the ultra-wide lens is used only when iOS switches to it for close focus (macro). The ultra-wide alone is never chosen: its distortion defeats the reader. Pass `device:` to the initialiser to use another camera.
- **Distortion and close focus.** Geometric distortion correction is enabled where the device supports it. Where the lens cannot focus on a typical code held to fill the frame (`minimumFocusDistance`), the resting zoom is raised, by at most 2x, so the user holds the phone where it can. `scanner.cameraControl` has the switches (`distortionCorrection`, `closeFocusZoom`, `typicalCodeWidth`).
- **Scan region.** Set `scanner.scanRegion` to your reticle in capture-device coordinates (`CKeyPreviewView.sensorRect(fromViewRect:)` or `CKeyPreviewGeometry.sensorRect(fromViewRect:)`). Decoding reads it first, and focus and exposure rest on its centre. The default is the middle of the frame.
- **Frame rate.** Frames are analysed off the main thread at up to `maxFramesPerSecond` (default 20). Tracking is not capped.
- **Capture resolution.** `.uhd4K` (default) or `.hd1080`, in the initialiser.
- **Camera access.** `scanner.cameraAuthorization` (`.notDetermined`, `.authorized`, `.denied`, `.restricted`, `.unavailable`) for the UI, and `await scanner.requestCameraAccess()` to ask without starting.
- **Torch.** `scanner.isTorchAvailable` and `scanner.setTorch(_:)` (returns whether it is on); stopping the camera turns it off.
- **Preview.** `CKeyCameraPreview(session:geometry:interestRect:onRegionOfInterest:)` keeps the image upright in every orientation and posture with `AVCaptureDevice.RotationCoordinator` (iOS 17 and later), and reports the reticle (`interestRect`, in the preview's coordinates) in sensor coordinates for `scanRegion` whenever layout or rotation changes it.

### Tracking and your own overlay

Tracking is separate from decoding and keeps going after a read. On every frame the core re-detects the code in a small crop around its last corners (`ckey_track_gray8`), so the corners are measured, not extrapolated. A candidate must fit the CKey keys: a strong fit or a decode locks at once, and a weak one needs a second sighting. A track survives up to eight missed frames. A located or decoded scan re-seeds it.

`onTrack` is the raw data, for any overlay:

```swift
scanner.trackQueue = .main                 // any queue you choose
scanner.onTrack = { track in               // nil: the track was lost
    guard let track else { return myOverlay.hide() }
    track.corners        // 4 points, normalized sensor coordinates, in a stable order across frames
    track.captureTime    // host-clock seconds (the clock of CACurrentMediaTime and CADisplayLink)
    track.score          // fit score
    track.decoded        // true once this code has decoded
    if let points = previewView.viewPoints(fromSensorPoints: track.corners) { myOverlay.show(points) }
}
```

`CKeyCameraPreview` converts normalized sensor points to view points with `AVCaptureVideoPreviewLayer.layerPointConverted(fromCaptureDevicePoint:)`, which accounts for aspect-fill cropping and rotation:

- UIKit: `CKeyPreviewView.viewPoints(fromSensorPoints:)`, `viewRect(fromSensorRect:)` and `sensorRect(fromViewRect:)`.
- SwiftUI: `CKeyCameraPreview(session:geometry:)` fills a `CKeyPreviewGeometry` with the same three functions.
- Your own preview layer: `CKeyCameraPreview.viewPoints(fromSensorPoints:in:)`.

`captureTime` is the frame's presentation timestamp converted to the host clock, so an overlay can time its motion by when the camera saw the code. `CKeyBracketMotion` is the smoothing the built-in brackets use (below), and it works on any four points. `CKeyCorners` has the corner helpers (`matched(_:to:)`, `expanded`, `centroid`).

### The optional bracket overlay

`CKeyBracketOverlay` (a `UIView`) and `CKeyBracketOverlayView` (SwiftUI) draw brackets that snap onto the tracked code and lean with it. They are opt-in: nothing is drawn unless you add the view.

```swift
@StateObject private var geometry = CKeyPreviewGeometry()
private let feed = CKeyTrackFeed()         // scanner.onTrack = { feed.publish($0) }

ZStack {
    CKeyCameraPreview(session: scanner.session, geometry: geometry)
    CKeyBracketOverlayView(feed: feed, geometry: geometry) { overlay in
        overlay.color = .white             // styling and tunables, all optional
        overlay.lineWidth = 4
    }
    .allowsHitTesting(false)
}
```

In UIKit, lay a `CKeyBracketOverlay` exactly over the preview and set `feed` and `previewLayer`.

The brackets are `CAShapeLayer`s stepped by a `CADisplayLink` on every screen refresh. The corners reach the view through the lock-protected `CKeyTrackFeed`, never through SwiftUI state. Motion is smoothed in screen space in three steps:

1. A **One Euro filter** on the eight corner coordinates, timed by capture time: `minCutoff` 1.2 Hz, `beta` 0.02, derivative cutoff 6 Hz.
2. A **latency projection**: the filtered target is moved along its velocity by `leadGain` (0.75) of the time from capture to display, capped at 120 ms.
3. A **critically damped spring** (`response` 0.08 s), integrated exactly per display frame.

| Tunable | Default | Effect |
|---|---|---|
| `minCutoff` | 1.2 | Lower is steadier when still; higher follows sooner. |
| `beta` | 0.02 | Higher follows fast motion more closely. |
| `response` | 0.08 | Lower is snappier. |
| `leadGain` | 0.75 | Share of the latency projected ahead; 0 turns it off. |

Styling: `color`, `haloColor`, `highlightColor`, `highlightHaloColor`, `isHighlighted`, `highlightsDecoded`, `lineWidth`, `haloWidth`, `cornerLength`, `minimumCornerLength`, `padding` and `minimumSize`. `onShowingChange` tells you when the brackets are on a code, for example to hide a resting reticle. For 120 Hz on iPhone, add `CADisableMinimumFrameDurationOnPhone` to the app's Info.plist.

### Guidance

`onGuidance` reports what to tell the user, as a `CKeyGuidance` option set, steadied over a few frames so a hint does not flicker:

| Flag | Meaning |
|---|---|
| `.moveCloser` | The code is too small in the frame. |
| `.moveBack` | A corner of the code is cut off by the frame edge. |
| `.holdSteady` | The image is blurred. |
| `.refocus` | The code is out of focus (the scanner also restarts autofocus). |
| `.glare` | Part of the code is washed out: tilt the phone or the surface. |

`guidance.primary` picks the one hint to show when several are set.

### Bring your own camera

A host that owns its `AVCaptureSession` uses `CKeyFrameProcessor`, the same engine `CKeyCameraScanner` runs. It decodes, tracks and advises; it never touches a capture device.

```swift
let processor = CKeyFrameProcessor()
processor.onResult = { result in show(result) }               // main queue
processor.onTrack = { feed.publish($0) }
processor.onGuidance = { guidance in showHint(guidance) }
processor.scanRegion = previewView.sensorRect(fromViewRect: reticle)

// AVCaptureVideoDataOutputSampleBufferDelegate
func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
    guard let buffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
    processor.process(pixelBuffer: buffer,
                      captureTime: CKeyCameraScanner.captureTime(of: sampleBuffer, session: session))
}
```

`process(pixelBuffer:captureTime:)` returns at once; decoding and tracking run on the processor's own queues. `process(gray:width:height:stride:captureTime:)` takes a raw 8-bit luma frame and returns the scan. Call `scanAgain()` for the next code. Keep the video data output's connection unrotated and unmirrored, so normalized coordinates are capture-device coordinates.

To let the SDK configure and drive **your** capture device, give its advice to a `CKeyCameraControl`:

```swift
let control = CKeyCameraControl(device: myDevice)
control.exposureControl = false                     // opt out of what the host manages itself
control.prepare()                                   // once the session's format is final
processor.onAdvice = { control.apply($0) }
control.onZoomChange = { processor.zoomDidChange(by: $0) }
```

Or read `CKeyCameraAdvice` yourself (`exposureBiasEV`, `zoomFactor`, `point`, `guidance`, `reaim`, `sharpness`, `contrast`, `clippedFraction`) and apply it your own way.

### Payload actions

```swift
if let text = result.text, let action = CKeyPayload.classify(text) {
    switch action {
    case .web(let url):                       showButton("Open \(url.host ?? "link")")
    case .email(_, let address):              showButton("Email \(address)")
    case .call(_, let number):                showButton("Call \(number)")
    case .message(_, let number):             showButton("Message \(number)")
    case .geo(_, _, let label, let mapURL):   showButton("Show \(label ?? "location") in Maps", mapURL)
    case .address(let address, let mapURL):   showButton("Show \(address) in Maps", mapURL)
    case .contact(let vCard, let name):       showButton("Add \(name) to Contacts…", vCard)
    }
}
```

`classify` follows `spec/CKey-Payload-Conventions.md`. It never acts on a payload. Only the allowlisted forms get an action, and each one is validated first. Everything else returns `nil` and is shown as plain text, including `javascript:`, `data:`, `file:`, `intent:` and custom schemes. Act only when the user taps, and keep the payload visible.

## Safe Actions

A Safe Action (`ckey-action:1:…`) is a signed code from a known site for this app. `CKeySafeActions` verifies it with the core and decides whether it runs; the app only pins keys and registers handlers.

```swift
let actions = CKeySafeActions(appID: "com.viadere")
    .trust(issuer: "citeory.com", keyID: "2026-11", publicKeyHex: "<64 hex digits>",
           requestInfo: "https://citeory.com/api/ckey/requests/{request_id}")
    .register("open.library", issuers: ["citeory.com"]) { action in openItem(action.paramsText) }
    .register("signin.request", issuers: ["citeory.com"], scope: .crossDevice) { action in approveSignIn(action) }

switch await actions.handle(text) {
case .notASafeAction: showPayload(text)                  // the ordinary payload rules
case .performed: break                                    // same-device: already ran
case .needsConfirmation(let request): pending = request   // present CKeySafeActionPrompt(request: request)
case .rejected(let reason): showUnverified(reason)
}
```

Same-device actions run at once. Cross-device actions run only from `CKeySafeActionPrompt`, which shows the verified issuer and who is asking; there is no other way to approve one. Anything that fails a check runs nothing; show that the code claims to be a Safe Action but could not be verified, with the reason. See [spec/CKey-Safe-Actions.md](../../spec/CKey-Safe-Actions.md).

## Telemetry

The `CKey` product keeps aggregate scan counters by default (mode 1) and sends them to `https://openckey.org/api/telemetry/v1/batch` at most once an hour. The normative rules are in [docs/adoption-and-telemetry.md](../../docs/adoption-and-telemetry.md).

**Opt out in one line**, at launch and before scanning:

```swift
CKey.telemetry = .off      // or: CKey.telemetry = 0
```

| Mode | Swift | Counts locally | Sends |
|---|---|---|---|
| 0 | `.off` | no (switching purges every counter) | no |
| 1 | `.on` (default) | yes | aggregate batches, at most hourly |
| 2 | `.local` | yes | never |

- `CKey.telemetry` is a `CKeyTelemetryMode`. Integer literals `0`, `1` and `2` also work (`CKey.telemetry = 2`). Any other literal is a programming error that stops the program; it is never treated as 1.
- For values known only at run time, such as those from a configuration file, use `try CKey.setTelemetry(rawValue: n)`. It throws `CKeyError` with code `.invalidConfiguration` for anything other than 0, 1 or 2, and leaves the mode unchanged. `CKeyTelemetryMode(rawValue:)` is failable.
- `CKey.telemetryCounters` returns the local totals (modes 1 and 2).
- The mode is process-wide and is not persisted.

**What is counted:**
- scan sessions: started, succeeded and failed;
- processed frames;
- decodes per profile;
- time-to-first-decode and per-frame histograms;
- the SDK name (`swift`) and version, the platform (`ios` or `macos`) and the UTC day.

**What is never collected:**
- payloads or URLs;
- user, device, installation or advertising IDs;
- location;
- per-scan histories.

**How sessions are recorded:** a session begins at `start()` and at `scanAgain()`, succeeds at its first valid decode, and fails at `stop()` or after 30 s without a decode. If you run your own camera pipeline, report its sessions through `CKeyTelemetrySession.shared`: pass it as the observer of your `CKeyFrameProcessor` (frame timings and the decode) and call `scanSessionDidBegin()` and `scanSessionDidEnd()` yourself. The camera layer adds no data collection of its own: tracks, advice and guidance stay in the process.

**How batches are sent:**
- The core decides when a batch is due. The SDK checks every 15 minutes, after each session ends and when the app moves to the background (`UIApplication.didEnterBackgroundNotification` on iOS, `NSApplication.didResignActiveNotification` on macOS).
- Each batch is one HTTPS POST through an ephemeral, cookie-less `URLSession`, made on a private utility queue and never on the scan queue.
- On iOS, the background attempt runs under `ProcessInfo.performExpiringActivity`, so it also works in app extensions.
- A failed send keeps the batch for a later attempt.

**Privacy manifest:** the `CKey` product ships a `PrivacyInfo.xcprivacy` in its resource bundle. It declares:
- Product Interaction, used for Analytics, not linked to the user and not used for tracking;
- no tracking domains;
- a monotonic clock used to measure elapsed time between events (reason `35F9.1`).

Integrators still make their own App Store disclosures. Apps that must not contain telemetry at all should use the `CKeyScanner` product instead.

## Develop

```sh
swift build
swift test
```

The tests (XCTest) cover:
- encode → `CGImage` → decode round trips for every raster policy and both layouts, including the `CVPixelBuffer` path;
- PNG, PDF and SVG output;
- transparent alpha;
- style refusal and override;
- certification;
- telemetry mode validation and local counting;
- payload classification;
- the camera layer's pure parts: the One Euro filter, the critically damped spring, the bracket motion, corner utilities, guidance, region arithmetic and `CKeyFrameProcessor` on synthetic frames;
- the Ultra and Extreme levels and the grouped layouts.

The capture code (`CKeyCameraScanner`, `CKeyCameraControl`, `CKeyBracketOverlay`) needs a device and has no automated tests.

CI runs `.github/workflows/sdk-swift.yml` on `macos-14`.
