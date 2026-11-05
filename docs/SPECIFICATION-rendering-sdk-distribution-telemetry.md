# CKey — Rendering, SDK, Distribution, Adoption & Telemetry Specification

**Status:** Working specification / discussion capture
**Date:** 2026-11-04
**Scope:** Consolidates the entire supplied conversation. Intended for implementation agents and maintainers.
**Normative language:** MUST = agreed requirement; SHOULD = preferred direction; PROPOSED = not yet finalized; RESEARCH = requires testing. The document separates expressed user intentions from assistant suggestions. No claim is made that features are implemented.

## 1. Vision and success criteria

CKey is an open-source, visually intentional machine-readable optical code with square matrix regions, orientation finders, radial rays, and concentric rings. It is intended to offer a visually balanced alternative to QR without compromising fast, dependable scanning. Viadere is an initial application-specific adopter; the larger goal is open-world scanning, ideally in the native iOS and Android camera applications. Native camera compatibility is a fundamental design objective from day one, not a merely optional future accelerator. CKey should arrive as a ready-to-integrate detector, finder, geometric normalizer, decoder, and optional ML-assisted scanner, so a platform camera team does not have to build the decoding stack from scratch.

Key success dimensions: small decoder footprint, fast detection and decoding, low false-positive rates, reliable performance across real cameras, accessible multi-platform packages, a rigorous open standard, and eventual camera-platform support. QR remains essential in many industries; CKey is not intended to replace every incumbent symbology. Data Matrix's industrial prevalence and possible phone support illustrate that consumer recognition and platform support are distinct. Native OS barcode framework support must not be conflated with automatic scanning in the default camera app; verify capabilities by OS/version.

### 1.1 Deployment phases

1. First-party integrations such as Viadere, which control both generation and scanning.
2. Independent apps with embedded CKey scanner libraries; a standalone CKey scanner app is also PROPOSED.
3. Electronic displays may show CKey by default with an immediately available *Show QR instead* fallback. Avoid making app installation mandatory for ordinary customers.
4. Long-term: native default-camera support by Apple/Google, with measurable reliability and demand evidence.

Physical business cards and static signage are higher-friction until default cameras support CKey. Standalone scanner installs and SDK usage may help adoption, but no specific user-count threshold guarantees platform integration. Early outreach with a mature, benchmarked implementation is appropriate; adoption is a separate factor.

## 2. Rendering architecture — core distinction

**Decision:** Do not make SVG the canonical internal representation. Build a purpose-built mathematical geometry model and lightweight deterministic C/C++ rendering core; SVG remains an export backend, not the mandatory rendering pipeline. The renderer is headless: it has no visible UI, chrome, border, background widget, or iframe. The host supplies a display surface or consumes pixels/paths. A transparent output contains only the drawn code geometry; unpainted pixels have alpha zero.

### 2.1 Geometry engine

The geometry engine MUST define finders, matrix modules, rays, rings, boundaries, and exclusion/quiet zones in one coordinate system. Geometry must be deterministic for identical symbol/profile/settings. The implementation must prohibit square grid modules appearing behind orientation finders, preserve safe gaps around finders, and avoid dead regions caused by incorrect renderer layout. These constraints were explicitly identified in prior CKey work and remain important to the new renderer. Radial-ray widths are to remain fixed when adjusting ray lengths unless a deliberately versioned geometry profile changes that contract.

Separate (a) symbol payload and encoding, (b) mathematical symbol geometry, (c) raster/vector backends, (d) display adapters, and (e) scan/decode libraries. Do not force an application that only scans to bundle the renderer.

### 2.2 Raster backend

A custom rasterizer can allocate an RGBA or grayscale pixel buffer and evaluate pixel coverage for squares, lines, arcs, and rays. Candidate policies include:

- **Hard binary thresholding:** fully paint a pixel if coverage exceeds a defined threshold (e.g., 50%).
- **Coverage antialiasing:** use intermediate intensities according to area coverage.
- **Connectivity/separation-aware hybrid:** preserve thin-ray continuity and gaps at a target pixel size.

RESEARCH: No one strategy is universally superior. Test scan success after camera blur, sampling, rescaling, compression, glare, and print artifacts. Antialiasing is not inherently bad; removing it can introduce stair-stepping or broken features. Benchmark rather than assume.

A raster buffer is not an image file. It can be displayed directly by native platform APIs, or encoded to PNG for export. The core library should be small and avoid mandatory large graphics stacks such as Skia. Skia may still be a comparison baseline or optional backend; its actual package size varies by build/platform.

### 2.3 Vector backend

Produce exact geometry as SVG and/or PDF vector paths when needed. SVG's limitations for this use case include variable downstream rasterization behavior, parsing/DOM overhead for large numbers of primitives, and lack of intrinsic CKey-specific optical constraints. SVG itself is not geometrically imprecise: it can express exact vector paths, but final pixel sampling depends on the renderer and device. A custom engine should own the CKey geometry and may emit optimized SVG/PDF without needing to embed a full SVG engine.

### 2.4 Screen versus print

**Screen backend:** optimize pixel placement, minimum resolvable gaps, and edge treatment for target pixel dimensions/device scale. Produce native RGBA buffers and optionally PNG. Avoid untested auto-adjustments that alter the encoded topology.

**Print backend:** retain vector paths where possible (PDF/SVG), specify physical dimensions and minimum features, and optionally apply print-process compensation for ink/toner spread. Printing compensation requires measured printer/media profiles and cannot be assumed from DPI alone. Symbol payload and encoding are unchanged across outputs.

### 2.5 Output types

| Output | Purpose |
|---|---|
| Native RGBA/gray pixel buffer | Headless in-memory rendering to UI surfaces |
| PNG | Lossless portable raster export |
| SVG | Vector export and browser/print interoperability |
| PDF vector | Print and professional graphics workflows |
| `.ckey` (PROPOSED) | Compact native interchange of payload/profile/geometry metadata, not required to display |

A `.ckey` file is optional and must not become a dependency of normal in-memory rendering. Define versioning, bounds, parser security, and whether payload content is embedded before standardizing it.

## 3. Host integration and visual behavior

**Web:** WebAssembly-compiled C/C++ core calculates pixels; JavaScript bridges the pixel buffer into an HTML `<canvas>` via `ImageData`/`putImageData`, or exposes PNG/SVG as alternative outputs. Canvas is an ordinary element in the document, not an iframe. A wrapper may offer `CKey.render(data, canvas, options)`; the exact API is illustrative. Rendering may also be performed with WebGL/WebGPU or platform APIs later, but these are not required.

**Swift:** C/C++ core via stable C ABI or C++ interoperability, thin Swift package, native pixel buffer -> `CGImage`/`UIImage`/`NSImage` -> SwiftUI `Image` or view. Illustrative usage: `CKeyView(data: "ABC123")`. No HTML, SVG viewer, or intermediate file required.

**Android:** NDK library + Kotlin/Java wrapper, native pixels -> `Bitmap`/Compose/View. **.NET:** native library with P/Invoke or supported interop, pixel buffer -> host-specific image control. Provide separate headless core and optional UI adapters so integrators can choose.

A transparent renderer must produce no visible container chrome and must not paint a background unless requested. The host decides placement, scaling, clipping, and compositing. The SDK should expose both headless rendering and convenient native UI adapters; platform display adapters need not ship when not used.

## 4. Dynamic styling, transparency, and optical safety

CKey styling SHOULD permit configurable foreground/background colors, textures, images, dimensions, and potentially 3D-like visual treatments. Styling must preserve essential detector and decoder geometry. Visual styling is subordinate to machine readability.

**Default:** classic high-contrast black on white when no style is provided and an opaque background is acceptable. **Transparent mode:** do not assume white behind the symbol. If the host is dark gray, black geometry may be unreadable. For safe transparency, host MUST supply background color(s), rendered pixels, or an appropriate sampling/compositing interface if the renderer is to certify contrast. CSS backgrounds, gradients, video, and dynamically changing content may make a single background color inadequate. If the background is unknown, the renderer must not falsely certify readability.

Contrast can be assessed with relative luminance/contrast ratios, but conventional WCAG text thresholds are not proven barcode-scanning thresholds. Test luminance separation at the feature level, effective composited alpha, blur, local feature contrast, and expected camera capture. Textures, embedded images, and perspective effects require additional testing.

PROPOSED policy modes: **strict/certified** (enforce scan-safety constraints), **adaptive** (adjust permissible styling and report changes), **unrestricted/override** (explicit developer override with warnings). User favored enforcement by default with an override; precise thresholds and whether output is blocked are not finalized. Every modification/warning should be inspectable. Optical certification must be tied to an explicit capture/print test model, not asserted from contrast ratio alone.

## 5. Research and validation program

Investigate six approaches without assuming improvements:

1. Self-calibrating finder/ring geometry for scale, rotation, perspective, and local warp.
2. Camera-aware module/ray geometry optimized for pixel sampling.
3. Region-specific adaptive density while respecting minimum feature sizes.
4. Print compensation for measured dot gain and output processes.
5. Alternative radial signal encoding, including angular/radial transitions, versus discrete binary modules.
6. Distributed alignment references resilient to partial occlusion.

Create a renderer–decoder optimization test harness: generate candidate symbols, apply realistic camera/print degradations, decode with the actual CKey engine, and measure successful payload recovery, bit errors, false positives, latency, and physical size. Compare against QR and Data Matrix with equivalent payload, area, and acquisition conditions. Optimize recoverable payload per physical area, not theoretical drawn density. No unverified claim of greater density or faster scans should appear in public specs.

## 6. Scanner architecture and platform-camera readiness

The scanner is a first-class product, not an afterthought. Provide reusable components for detection, finder localization, orientation normalization, payload decoding, and optional ML-based recognition. Decoding must work offline and should not require telemetry, remote services, or CKey-controlled servers. Separate detection from decode so platform camera pipelines can adopt only needed components. Target rapid live-camera scanning; prior project ambition was sub-200 ms, which must be defined by device, image size, payload profile, and percentile before being treated as an acceptance criterion.

Camera integration should support continuous frame processing and emit a result as soon as a valid symbol is decoded, without requiring a captured still. Test orientation ambiguity, false-positive rejection, mixed backgrounds, finder interference, and scan-time instrumentation. Provide portable C/C++ core and appropriate SDK wrappers, sample apps, conformance vectors, reproducible benchmarks, and a security review surface for native-camera integrators. Open-source availability alone does not guarantee Apple/Google inclusion; camera APIs, system integration, policy, performance, and maintenance decisions remain theirs.

## 7. SDK packaging and developer installation

Maintain a canonical public repository, github.com/clearkeysolutions/ckey, with as few branches as practicable. Developers who want to use CKey should obtain packages through native ecosystems; contributors who want to advance CKey primarily use GitHub.

| Ecosystem | Distribution | Typical integration |
|---|---|---|
| Swift/iOS/macOS | Swift Package Manager, source or XCFramework binary target | Add package URL in Xcode, `import CKey` |
| Android | AAR via Maven-compatible repository | Gradle dependency |
| Web | npm package with JS/TS and WASM | `npm install` + import |
| .NET | NuGet with runtime-specific native assets | `PackageReference` |
| C/C++ | Source/build package, optional prebuilt artifacts | CMake/package manager |
| macOS CLI | Homebrew (PROPOSED) | `brew install` for tooling, not a substitute for Swift package |

A Swift package URL ordinarily points to a Git repository with a `Package.swift` manifest at its package root; a nested SDK folder is not automatically a directly installable package URL. A single monorepo can still publish language-specific artifacts with the appropriate manifests, packaging, or dedicated package roots/mirrors. Ship source plus signed/checksummed release artifacts where applicable. Apple notarization is primarily for distributable macOS software, not a universal requirement for iOS SDK packages. Build and publish from trusted tagged commits, not uncontrolled community code.

## 8. Repository governance and release strategy

Preferred initial branch model: protected stable (or main, if naming changes) and an experimental community branch. The user expressed interest in permitting experimentation without a mandatory maintainer review of every community change. GitHub still requires write permission for direct pushes; public visibility does not grant public push rights. Signed commits prove association with a signing key, not developer trustworthiness. An automated fork-to-community merge workflow could allow broader participation but needs abuse controls, CI, and isolation of secrets.

Recommended safeguards: protect official release branch and tags, require CI for release promotion, disallow community-originated workflows from accessing publishing credentials, and document that community is unstable and must not be used as the production dependency source. Developers should fork or branch from stable, test locally, and propose changes to community. Significant work may be preserved in a protected feature/maintenance branch. Revert/reset experimental code as needed; avoid casually rewriting branches others depend on.

### 8.1 Versioning and preservation of dev history

The user wants to retain the actual iteration/build count (e.g., dev 60) visibly, rather than discard it at first public release. Git tags capture exact historical source snapshots, and GitHub Releases add presentation, notes, and artifacts. There is no need for one branch per version. One permanent stable branch plus immutable version tags is preferred. Developers can browse any older tagged source tree after later releases.

PROPOSED version convention: SemVer precedence plus build metadata, e.g., `v0.1.0+build.60`, with prereleases such as `v0.1.0-dev.60`. Build metadata retains the count but does not affect SemVer ordering. A plain four-part version `0.1.0.60` is not SemVer and may be handled inconsistently by ecosystems; prefixing the count `60-0.1.0` is also nonstandard. The exact scheme remains a decision for the coding agent and maintainer. Use a machine-readable build/iteration counter if the count must remain globally monotonic across releases.

Do not rename the stable branch for every release. Git tags can be renamed only by creating another tag and deleting the former, which disrupts consumers; treat published version tags as immutable. Multiple tags may point to the same commit, but a GitHub Release has one associated tag.

### 8.2 Deprecation lifecycle

Deprecation means discouraged for new use; it does not necessarily mean deleted, broken, or immediately unsupported. GitHub does not provide a dedicated per-release deprecated badge/status pill. Workarounds: prefix the visible release title with DEPRECATED, update release body via GitHub CLI/API, maintain a machine-readable version support matrix, and propagate package-registry deprecation where available. Release titles/descriptions can be edited through CLI, without changing the underlying immutable tag. npm supports package-version deprecation messages; other registries have different capabilities and should be evaluated individually. A deprecated GitHub repository topic would apply to the entire repository, not one release.

## 9. Adoption measurement and privacy-respecting telemetry

A third-party app can embed an offline decoder, and CKey cannot know every scan globally. Adoption evidence may include optional SDK-reported aggregate scan metrics, package downloads (not equivalent to active installations), GitHub activity, voluntarily registered apps/businesses, and opt-in redirect statistics. Public figures MUST distinguish observed/reported counts from estimated total usage.

User's chosen telemetry modes and numeric API contract:

| Value | Canonical name | Collection | Transmission |
|---|---|---|---|
| `0` | `CKEY_TELEMETRY_OFF` | None | None |
| `1` | `CKEY_TELEMETRY_ON` | Local aggregate counters | Sends aggregate telemetry to CKey; default |
| `2` | `CKEY_TELEMETRY_LOCAL` | Local aggregate counters | Never sends to CKey |

Use numeric 0, 1, 2 as canonical cross-language values. Developers may be given language-idiomatic named constants/enums. Illustrative settings: `CKey.telemetry = 0`, `CKey.telemetry = 1`, `CKey.telemetry = 2`; exact syntax varies by language. An earlier `.local` spelling was considered but numeric 2 supersedes it for cross-language portability. Invalid values MUST raise a configuration error rather than silently default to 1. 1 is the desired default, with obvious documentation and a one-line opt-out; external distribution/privacy review may require adjustments in specific environments.

### 9.1 Aggregate schema

Collect counts of scan attempts, successes, failures, and aggregate decoding durations (e.g., count, sum, histogram buckets), plus scanner/SDK version as needed. Do not collect code payloads, URLs, scanned content, user IDs, device IDs, device fingerprints, location, or per-scan event histories. A scan attempt must be defined carefully in continuous camera mode (e.g., scanning session vs processed frame), or metrics will be misleading. A failed attempt needs an explicit timeout/end-of-session definition. Avoid sending one network request per scan; aggregate locally and send batches asynchronously. Telemetry must never delay, block, or be required for scanning.

Mode 2 must make counters available locally to the host application, with no telemetry transmission. Mode 0 stops counting and transmission. Switching from mode 1 to 2 or 0 must prevent further outbound reports, including queued batches; define whether previously stored aggregates are retained or purged. Persistence across app restarts is an optional configuration distinct from telemetry mode.

### 9.2 Data minimization and transport controls

The telemetry SDK should omit identifiers and use a strict minimal schema; the ingestion service should reject unrecognized fields and avoid retaining source IPs in application logs. The destination service having its own dedicated IP does not conceal the client's source IP: IP routing, proxies, CDN logs, and abuse defenses can still observe it. Use an appropriately configured proxy/logging policy, data retention limits, and operational safeguards. Avoid claiming there is zero privacy impact solely because the payload contains counters; laws, IP processing, and app-store disclosure requirements may apply. Document telemetry plainly and permit opt-out. Scanner builds for OS vendors should be able to omit the telemetry module entirely.

### 9.3 Security and integrity

Default-on telemetry from open SDKs is inherently unauthenticated/untrusted unless extra controls are added; anonymous counters can be spoofed, replayed, or inflated. Apply bounded schemas, rate limiting, abuse detection, and transparent methodology. Do not claim global unique users or exact worldwide scans from these reports. Prefer coarse aggregate metrics and no stable installation identifiers; understand the tradeoff that robust deduplication becomes difficult without identifiers.

## 10. Suggested source layout (PROPOSED)

```
ckey/
  README.md
  LICENSE
  SPECIFICATION.md
  docs/
    optical-profiles/
    sdk-api/
    adoption-and-telemetry.md
    support-matrix.json
  core/
    encoding/
    geometry/
    raster/
    vector/
    detection/
    decoding/
    telemetry/
  sdk/
    swift/
    android/
    web/
    dotnet/
    c-cpp/
  tools/
    cli/
    conformance/
    optical-benchmarks/
  examples/
    web/
    ios/
    android/
  .github/workflows/
```

This is an illustrative monorepo layout; package manifests must match each ecosystem's installation requirements. Ensure all SDKs share the same core behavior and conformance fixtures.

## 11. Implementation acceptance criteria

1. Identical payload/profile yields identical canonical geometry across all targets.
2. Raster output is deterministic for a defined size, style, and raster policy.
3. Finder exclusions, region boundaries, quiet/safe zones, and ray-width invariants are tested.
4. Headless renderer works without a visible UI component, file system, network, or SVG interpreter.
5. Transparent output correctly composites with host background; unknown backgrounds cannot receive false readability certification.
6. Scanability checks report contrast, geometry, and modeled acquisition constraints; real camera tests verify claims.
7. Web canvas, Swift, Android, and .NET integrations display native pixels without intermediate files.
8. Scanner remains offline-capable and does not require the renderer or telemetry module.
9. SDKs are consumable through their native package ecosystems; official releases come from trusted commits.
10. Historical releases remain retrievable by immutable tags, and deprecated versions have visible lifecycle metadata.
11. Telemetry modes 0/1/2 behave identically across SDKs, with 1 default, 2 local-only, and 0 fully disabled.
12. No scan payloads or user/device identifiers are transmitted by the telemetry SDK.
13. Telemetry does not impair decoding performance, and reported adoption is labeled accurately.

## 12. Outstanding decisions for the coding agent

- Final CKey geometry/profile schema and version compatibility rules.
- Exact anti-aliasing/pixel-coverage algorithm and thresholds, based on benchmarks.
- Print compensation calibration and whether it is enabled automatically.
- Optical safety policy modes, override semantics, and warning/error format.
- Exact color/texture/image styling schema and supported 3D-like transformations.
- Whether `.ckey` interchange format is warranted; if so, binary layout and security limits.
- Whether scanner/renderer packages are separate modules in each ecosystem.
- Precise version/build counter format preserving iteration history.
- GitHub branch governance for experimental community contributions without mandatory manual approvals.
- Telemetry schema encoding, batch cadence, retention, anti-abuse strategy, and persistence.
- Native-camera benchmark test plan and public integration documentation for Apple/Google.

## 13. Explicit conversation corrections and caveats

- A GitHub public repository does not let anyone push directly; write access or controlled automation is still necessary.
- Signed commits alone are not a substitute for contributor authorization or code review.
- Tags are not disposable one-use artifacts; they are durable references to historical commits and can coexist at one commit.
- GitHub Releases lack a dedicated per-release deprecation status badge; editable release titles and registries are alternatives.
- A dedicated server IP does not hide client source IPs.
- SVG is a vector description, not inherently low-fidelity; the final rasterization and geometry choices matter.
- Headless rendering does not require a `.ckey` file, iframe, SVG, or browser; output can be an in-memory buffer.
- Transparent background does not automatically imply adequate contrast, and a renderer cannot know arbitrary underlying content without host cooperation.
- Native OS barcode recognition is not necessarily the same as default-camera-app recognition.
- Low integration effort and a stable open-source decoder increase feasibility but do not guarantee Apple/Google adoption.

---

*Document intent: A consolidated technical and governance specification preserving the user's expressed direction, prior assistant proposals, technical corrections, and unresolved research questions. It is not a claim of completed implementation or external expert review.*
