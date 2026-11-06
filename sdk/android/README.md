# CKey for Android

Kotlin/NDK SDK for [CKey](../../README.md) optical codes. It is a thin wrapper over the C++20 core's C ABI (`include/ckey/ckey.h`, `include/ckey/telemetry.h`). Encoding, decoding and rendering all happen in the core. The SDK converts arrays and turns pixels into `Bitmap`s.

| Artifact | Contents | Dependencies |
|---|---|---|
| `org.openckey:ckey` | Headless core: `CKeySymbol`, `CKeyDecoder`, `CKeyScanner`, `CKeyPayload`, `CKey` (telemetry), the classic `CKeyView`, and `libckey_jni.so` for arm64-v8a, armeabi-v7a, x86_64 and x86 | Kotlin stdlib only |
| `org.openckey:ckey-compose` | `@Composable CKeyImage` | `ckey`, Compose UI/Foundation |
| `org.openckey:ckey-camerax` | `CKeyAnalyzer` (`ImageAnalysis.Analyzer`) | `ckey`, CameraX core |

minSdk 24, compileSdk 34. Version = the repository's `VERSION` file (for example `0.1.0-build.77`).

## Install

```kotlin
// settings.gradle.kts: add the repository CKey is published to
dependencyResolutionManagement { repositories { google(); mavenCentral() } }

// app/build.gradle.kts
dependencies {
    implementation("org.openckey:ckey:0.1.0-build.77")
    implementation("org.openckey:ckey-compose:0.1.0-build.77")   // optional: CKeyImage
    implementation("org.openckey:ckey-camerax:0.1.0-build.77")   // optional: CKeyAnalyzer
}
```

To build from source, run `gradle -p sdk/android publishAllPublicationsToLocalRepository` (Gradle 8.10.2+, JDK 17+, Android SDK with NDK 27.2 and CMake 3.22.1). It writes a Maven repository to `sdk/android/build/maven`. The native build compiles the core sources in `src/` in place; nothing is copied.

## Render

```kotlin
val symbol = CKeySymbol.encode("https://openckey.org/tour")            // version 0 = smallest that fits, level M, STANDARD
val sized  = CKeySymbol.encode(bytes, version = 0, level = EccLevel.H, layout = Layout.PRINT)

val bitmap: Bitmap = symbol.toBitmap(sizePx = 600)                      // ARGB_8888, opaque, default style
val clear  = symbol.toBitmap(600, RasterPolicy.HYBRID, transparent = true, style = CKeyStyle.CONCEPT)
val png: ByteArray = symbol.toPng(1024, dpi = 300.0)
val svg: String    = symbol.toSvg(scale = 8)
val pdf: ByteArray = symbol.toPdf(widthPt = 144.0)                      // 2 inches
val rgba = symbol.toRgba(512)                                           // straight-alpha RGBA8, no Bitmap
```

* **Pixels.** The core renders straight (non-premultiplied) RGBA8. `toBitmap` premultiplies it in the bridge, because `Bitmap.copyPixelsFromBuffer` reads ARGB_8888 memory as premultiplied RGBA. With `transparent = true`, unpainted pixels have alpha 0.
* **Styles.** Start from `CKeyStyle.DEFAULT` (black on white) or `CKeyStyle.CONCEPT` and use `copy(...)`. Colours are `0xRRGGBB`. `policy` takes one of three values:
  * `StylePolicy.STRICT` (the default) refuses a style that fails the safety rules with a `CKeyException` that carries the core's reason.
  * `ADAPTIVE` lets the core repair the style.
  * `OVERRIDE` draws the style as given.
* **Checks.** `symbol.checkStyle(style)` explains a refusal. `symbol.certify(style, transparent, backdrop, model)` runs the core's modelled-acquisition certification. A transparent symbol is never certified without a backdrop.
* **Logo.** The raster backend draws `CKeyStyle.logo = CKeyLogo.fromBitmap(bmp)`. SVG export uses `logoHref`.

### Views

```kotlin
// Compose (ckey-compose)
CKeyImage(data = "https://openckey.org", modifier = Modifier.size(200.dp), style = CKeyStyle.DEFAULT)

// Classic View
val view = CKeyView(context).apply { data = "https://openckey.org"; onError = { Log.w("CKey", it) } }
```

Both are square and render at their exact pixel size off the main thread.

## Scan

```kotlin
// One frame of 8-bit gray (any stride), or a direct ByteBuffer, or a camera Image's Y plane
val result: CKeyResult? = CKeyDecoder.decode(gray, width, height, stride)
val fromImage = CKeyDecoder.decode(image)          // android.media.Image (YUV_420_888 / Y8); you close it
val fromBitmap = CKeyDecoder.decode(bitmap)        // photos, screenshots

result?.let {
    it.bytes; it.text /* null if not UTF-8 text per the conventions */; it.hex
    it.profile /* P0, P0_1, P1, P2, P3 */; it.version; it.layout; it.quad
    it.action  /* CKeyPayloadAction? — never performed automatically */
}
```

The decoder follows the iOS reader's pipeline:

1. It finds border candidates in both polarities.
2. For each candidate it refines the border to Profile 0's field and decodes Profile 0, which identifies the version and layout from the keys, using a 64 KB output buffer.
3. If that fails, it refines and decodes Profiles 1, 2 and 3, each against its own field.

### CameraX (ckey-camerax)

```kotlin
val analyzer = CKeyAnalyzer { result -> show(result) }      // runs on the main thread
imageAnalysis.setAnalyzer(cameraExecutor, analyzer)          // STRATEGY_KEEP_ONLY_LATEST, YUV_420_888
// later: analyzer.stop() when unbinding; analyzer.scanAgain() after showing a result
```

`CKeyAnalyzer` analyzes at most 20 frames per second by default. It is a thin layer over `CKeyScanner`, which you can drive from any camera stack: call `start()`, then `process(...)` for each frame, then `stop()`.

### Payload actions

```kotlin
when (val action = CKeyPayload.classify(text)) {
    is CKeyPayloadAction.Web     -> button(action.label) { open(Intent.ACTION_VIEW, action.uri) }  // "Open example.com"
    is CKeyPayloadAction.Email   -> button(action.label) { open(Intent.ACTION_SENDTO, action.uri) }
    is CKeyPayloadAction.Call    -> button(action.label) { open(Intent.ACTION_DIAL, action.uri) }  // never ACTION_CALL
    is CKeyPayloadAction.Message -> button(action.label) { open(Intent.ACTION_SENDTO, action.uri) }
    is CKeyPayloadAction.Location, is CKeyPayloadAction.Address -> button(action.label) { open(Intent.ACTION_VIEW, action.uri) } // geo: URI
    is CKeyPayloadAction.Contact -> button(action.label) { shareVcard(action.vcard) }
    null -> Unit                                                          // plain text: no action
}
```

Only the allowlisted forms in `spec/CKey-Payload-Conventions.md` produce an action. They are `http(s)` (with a host), `mailto` (with an `@`), `tel` and `sms` (at least 3 digits), `geo` (with a valid range), `address` (non-empty) and complete vCards. The URI schemes `javascript:`, `data:`, `file:` and `intent:`, and every other scheme, stay plain text. Always keep the payload visible next to the action.

## Telemetry

The default mode is 1. Only aggregate, anonymous scan counters are sent: session counts, frame counts, decodes per profile, timing histograms, the SDK version, the platform `android` and the UTC day. Payloads, URLs, user or device IDs and location are never collected. See [docs/adoption-and-telemetry.md](../../docs/adoption-and-telemetry.md).

To opt out, set the mode to 0 (one line):

```kotlin
CKey.telemetry = 0          // off: nothing counted, nothing sent, counters purged
CKey.telemetry = 2          // local only: counters via CKey.telemetryCounters(), never sent
CKey.telemetry = 1          // default
CKey.telemetry = 3          // IllegalArgumentException (never silently 1)
```

* **Recording.** `CKeyScanner` and `CKeyAnalyzer` record sessions. `CKeyDecoder` on its own records nothing.
* **Sending.** Sends run on one low-priority daemon thread, never on the scan thread, using `HttpURLConnection` to `CKEY_TELEMETRY_ENDPOINT`. There is at most one batch per hour.
* **When sends happen.**
  * When the core's `should_send` says a batch is due, checked after scan sessions and every 10 minutes.
  * When the app goes to the background, if `CKey.install(context)` was called. `CKeyView` and `CKeyImage` call it for you. This uses `Application.ActivityLifecycleCallbacks`, with no androidx.lifecycle dependency.
* **Permissions.** The AAR declares no permissions. Batches are sent only if your app already holds `INTERNET`.
* **Persistence across launches.** This is off by default. Use `CKey.exportTelemetryState()` and `CKey.importTelemetryState(json)`.
* **Disclosure.** Disclose telemetry according to your own store and legal requirements, for example the Play Data safety form ("App activity / app interactions", not linked to the user).

## Scan-only and render-only apps

* **Kotlin and Java.** Rendering, scanning and telemetry are separate classes, so R8 removes what an app does not reference.
* **Native code.** The published AAR contains every module (about 0.5 MB per ABI). Builds from source can compile parts out:

  ```
  gradle -p sdk/android :ckey:assembleRelease -Pckey.native.render=false   # scanner only
  gradle -p sdk/android :ckey:assembleRelease -Pckey.native.scan=false     # renderer only
  gradle -p sdk/android :ckey:assembleRelease -Pckey.native.telemetry=false
  ```

  A call into a module that was compiled out throws `UnsupportedOperationException`. The flags `CKey.hasScanner`, `CKey.hasRenderer` and `CKey.telemetryAvailable` report what is present. Without the telemetry module, `CKey.telemetry` reads 0 and nothing is ever counted or sent. The compose and camerax modules are optional.

## Tests

| What | Command |
|---|---|
| Unit tests (pure JVM: payload rules, telemetry validation, sender loop) | `gradle -p sdk/android :ckey:testDebugUnitTest` |
| Instrumented round trips on a device or emulator | `gradle -p sdk/android :ckey:connectedDebugAndroidTest` |
| Host JNI harness, no Android SDK needed | `sdk/android/jni-host-test/run.sh` |

The host JNI harness does the following:

* Checks the Kotlin `external` signatures against `javac -h` output.
* Builds `libckey_jni.so` for the desktop.
* Runs a Java harness: encode, render RGBA, convert to gray, decode; styles; exports; telemetry.
* If a Kotlin compiler is available (for example the one bundled with Gradle), compiles the Kotlin sources against small `android.*` stubs and runs the Kotlin API checks and the unit tests.

CI: `.github/workflows/sdk-android.yml`.
