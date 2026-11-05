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
| `CKeyScanner` | `import CKeyScanKit` | Scanning only: `CKeyDecoder`, `CKeyCameraScanner`, `CKeyCameraPreview`, `CKeyPayload`. It links the codec and scan libraries and nothing else, so it has no renderer and no telemetry. Use it in OS or vendor builds that must leave telemetry out entirely. |

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
```

`size` is the pixel size of the full 31-unit canvas, which includes the quiet margin. The raster policies are `.coverage` (antialiased, the default), `.binary` and `.hybrid` (thin marks kept at least 1 px wide). `CKeySymbol.capacity(version:level:layout:)` gives the payload limit for a version. `CKeyError` explains any refusal.

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

Headless, from any thread:

```swift
let result: CKeyResult? = CKeyDecoder.decode(cgImage: image)
let fromCamera = CKeyDecoder.decode(pixelBuffer: pixelBuffer)   // 420v/420f luma, gray8, BGRA/RGBA/ARGB
if let result {
    result.bytes        // Data, verified by Reed-Solomon and CRC-32C
    result.text         // String? — UTF-8 without control characters (tab/LF/CR allowed)
    result.profile      // .scalable, .locator, .text, .extended
    result.version      // Profile 0 version 1...31 (0 otherwise)
    result.layout       // .standard / .print for Profile 0
}
```

The pipeline is the iOS reader's (`ios/CKeyReader/CameraScanner.swift`). It finds up to six border candidates in the whole frame. It fits each one to the Profile 0 field and decodes it, then tries Profiles 1, 2 and 3, each refined against its own keys. Images are drawn over white, so a transparent rendering decodes as dark on white.

Live camera with `CKeyScanner` (in `CKey`, with telemetry sessions) or `CKeyCameraScanner(observer: nil)` (in `CKeyScanner`, without telemetry):

```swift
let scanner = CKeyScanner()
scanner.onResult = { result in            // main queue, first valid decode of the session
    show(result)                           // then call scanner.scanAgain() for the next one
}
scanner.start()                            // asks for camera access (NSCameraUsageDescription)
// SwiftUI: CKeyCameraPreview(session: scanner.session)
scanner.stop()
```

Frames are processed off the main thread at up to 20 per second. Late frames are discarded, and no still capture is needed. Call `start()`, `stop()` and `scanAgain()` from the main thread.

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

**How sessions are recorded:** a session begins at `start()` and at `scanAgain()`, succeeds at its first valid decode, and fails at `stop()` or after 30 s without a decode. If you run your own camera pipeline with `CKeyDecoder`, report its sessions through `CKeyTelemetrySession.shared`.

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
- payload classification.

CI runs `.github/workflows/sdk-swift.yml` on `macos-14`.
