# Implementation status against the Rendering, SDK, Distribution & Telemetry Specification

This document maps each section of [the specification](SPECIFICATION-rendering-sdk-distribution-telemetry.md) to the repository as of `0.1.0-build.82`, the first published release (`v0.1.0-build.82` on `Stable-Release`).

**Status key:**

- **Done:** exists and is tested in this repository's CI.
- **Done, CI-verified only:** written, but the toolchain is not available in the development container, so it is verified only by its GitHub Actions workflow.
- **Partial:** some of it exists.
- **Open:** needs a decision, hardware or data that the repository cannot supply.

No real-device, real-print or platform claim is made unless it was measured.

## Rendering (spec §2–§3)

| Item | Status | Where / notes |
|---|---|---|
| Canonical mathematical geometry (§2.1) | **Done** | `ckey_scalable_geometry` returns primitives in canonical units, in painter's order. The raster, PDF and SVG backends draw these. SVG agrees with the raster to within 0.01% of pixels (`ckey_native_render`). |
| Exclusions, safe gaps, no dead regions, fixed ray widths (§2.1, §11.3) | **Done** | `tests/layout_tests.cpp`, `tests/scalable_tests.cpp`. |
| Separate encoding / geometry / backends / scan (§2.1) | **Done** | `ckey_codec`, `ckey_scan` (needs only the codec), `ckey_render`, `ckey_certify` and `ckey_telemetry`. CI links a scan-only test against `ckey_scan` alone. See `src/README.md`. |
| Headless raster without an SVG interpreter (§2.2, §11.4) | **Done** | `ckey_render_scalable_raster`. The print path rasterizes the geometry directly, and the core no longer includes the SVG parser. |
| Raster policies: binary / coverage / hybrid (§2.2) | **Done** (policies); **Partial** (choice) | All three are implemented and decode. Which one is best per device is a benchmark question (§12), left open. |
| Native RGBA / gray buffers (§2.5) | **Done** | RGBA8 with straight alpha, and GRAY8. |
| PNG export (§2.5) | **Done** | `ckey_encode_png`: DEFLATE in the core, RGBA8, GRAY8 and 1-bit, with pHYs. |
| SVG export (§2.3) | **Done** | All profiles. |
| PDF vector export (§2.3–§2.4) | **Done** | `ckey_render_scalable_pdf`: exact paths, flat colours, optional transparency. |
| `.ckey` interchange (§2.5) | **Done** (draft v1) | `spec/CKey-Interchange-v1.md`. A strict reader, fuzzed. |
| Transparent output, alpha 0 where unpainted (§2) | **Done** | `transparent = 1` in raster and PDF. |
| Print backend: physical size, minimum features, compensation (§2.4) | **Partial** | Device-grid bitmaps, the Profile 0.1 print layout, PDF and a calibration sheet all exist. **Open:** measured printer/media profiles, which need real prints. |
| Web canvas / WASM (§3) | **Done, CI-verified only** | `sdk/web` (npm `ckey`). Its JS and native bridge tests run here; the wasm build runs in `sdk-web.yml`. |
| Swift package / `CKeyView` (§3) | **Done, CI-verified only** | `Package.swift`, `sdk/swift`. Its C++ shims are verified here. |
| Android (§3) | **Done, CI-verified only** | `sdk/android`. Its JNI bridge is verified on the JVM here. |
| .NET (§3) | **Done, CI-verified only** | `sdk/dotnet`. Its native library and struct layouts are verified here. |

## Styling and optical safety (spec §4)

| Item | Status | Where / notes |
|---|---|---|
| Colours, textures, images, materials (§4) | **Done** | `ckey_style`, with natively rendered materials and caller-supplied logo pixels. |
| Default black on white (§4) | **Done** | Since build 61. The concept look is `CKEY_PRESET_CONCEPT`. |
| Transparent mode needs the host background to certify (§4, §11.5) | **Done** | `ckey_backdrop`. With no backdrop the answer is "background unknown" and the style is never certified. |
| Certification tied to a capture model, not contrast alone (§4) | **Done** | `ckey_certify_scalable_style` renders, composites, degrades and decodes with the real decoder. |
| Policy modes strict / adaptive / override (§4) | **Done** | `ckey_style.policy` (ABI 4), with changes reported as `CKEY_ADJUST_*`. |

## Research harness (spec §5)

| Item | Status | Where / notes |
|---|---|---|
| Generate → degrade → decode → measure | **Done** | `ckey-bench`. |
| Recovery, bit errors, physical size, payload per area | **Done** (synthetic) | `docs/benchmarks/`. |
| False positives, latency percentiles | **Done** (synthetic) | p50, p90 and p99. |
| Comparison with QR and Data Matrix | **Done, CI-verified only** | Through zxing-cpp in the CI `benchmarks` job. |
| The six research directions (§5.1–5.6) | **Partial** | 1 is done. 2 is partly done, via Profile 0.1. 4 is simulated only. 3, 5 and 6 are open research. The harness exists to evaluate them. |

## Scanner (spec §6)

| Item | Status | Where / notes |
|---|---|---|
| Detection separate from decode | **Done** | `ckey_find_borders_gray8` → `ckey_refine_quad_gray8` → `ckey_decode_*_gray8`. |
| Offline; no telemetry or server needed | **Done** | `ckey_scan` has no network code and does not depend on telemetry. |
| Continuous frames, result on first decode | **Done** | iOS reader, and the scanner helpers in every SDK. |
| Optional ML recognition | **Partial** | Only a logistic candidate ranker. |
| Latency target by device, size, profile and percentile | **Open** | The harness reports percentiles. A device target needs real hardware. |
| Conformance vectors | **Done** | `tests/vectors/conformance-v1.json`, checked in CTest and consumable by SDKs. |
| Security review surface | **Partial** | Every parser is bounded: `.ckey`, the telemetry import, the service's JSON. The `.ckey` reader is fuzzed. No external review yet. |

## Packaging and governance (spec §7–§8)

| Item | Status | Where / notes |
|---|---|---|
| SPM / AAR / npm / NuGet / CMake package / Homebrew (§7) | **Done** (manifests and CI); **Open** (publishing) | Publishing needs the maintainer's registry accounts. |
| Signed, checksummed artifacts from tagged commits (§7) | **Done** | `release.yml`: the tag must match `VERSION` and be on `Stable-Release`. It produces `SHA256SUMS`, Sigstore provenance and a draft release. |
| Branch model and community isolation (§8) | **Done** | `docs/BRANCHES-AND-RELEASES.md`, `community.yml` (no secrets). Branch protection is set in GitHub's settings by the maintainer. |
| Immutable tags, deprecation titles (§8.1–8.2) | **Done** (tooling) | `deprecate-release.yml`, `docs/support-matrix.json`. First published release: `v0.1.0-build.82`. |
| Version counter preserves history (§8.1) | **Done** | `0.1.0-build.N`, tags `v0.1.0-build.N`. |

## Telemetry (spec §9)

| Item | Status | Where / notes |
|---|---|---|
| Modes 0 / 1 / 2; invalid values rejected | **Done** | `include/ckey/telemetry.h`, and every SDK. |
| Local aggregate counters, no identifiers | **Done** | `docs/adoption-and-telemetry.md`. |
| Session definition, batching, cadence, switching semantics | **Done** | Same document. |
| Ingestion service with abuse controls, no IP retention | **Done** (code); **Open** (deployment) | Runs on openckey.org, which is a separate, proprietary site, not part of this repository. It must meet the service obligations in `docs/adoption-and-telemetry.md`. |
| Scanner builds without telemetry | **Done** | `-DCKEY_BUILD_TELEMETRY=OFF`, the SwiftPM `CKeyScanner` product, the Android core module, and `CKEY_DOTNET_TELEMETRY=OFF`. |

## Acceptance criteria (spec §11)

| # | Status | Notes |
|---|---|---|
| 1 Deterministic geometry across targets | **Done** | The conformance geometry hash is identical under gcc -O0/-O2 and clang with fast-math. SDKs run the same vectors. |
| 2 Deterministic raster | **Done** | Same size, style and policy give identical bytes on one platform. Across platforms, pixels are checked by decoding. |
| 3 Exclusion, gap and ray-width invariants | **Done** | |
| 4 Headless without an SVG interpreter | **Done** | |
| 5 Transparent compositing, no false certification | **Done** | |
| 6 Scanability checks tied to a modelled acquisition | **Done** (model); **Open** (real cameras) | |
| 7 Native pixels in web, Swift, Android and .NET | **Done, CI-verified only** | |
| 8 Scanner independent of renderer and telemetry | **Done** | |
| 9 Native package ecosystems; releases from trusted commits | **Done** (tooling); **Open** (publishing) | |
| 10 Immutable tags, deprecation metadata | **Done** (tooling) | |
| 11 Telemetry modes identical across SDKs | **Done, CI-verified only** | Every SDK wraps the same C core. |
| 12 No payloads or identifiers transmitted | **Done** | Schema-enforced on both the client and the server. |
| 13 Telemetry does not slow decoding; adoption labelled accurately | **Done** | Recording is a few counter updates, sending is asynchronous, and the summary is labelled "not total usage". |

## What only the maintainer can do

- Deploy openckey.org (separate repository), which hosts telemetry ingestion, short destinations, the renderer and package downloads.
- Create the registry accounts (npm, Maven Central `org.openckey`, NuGet, a Homebrew tap) and publish from tagged releases.
- Protect `Stable-Release` and the tags in GitHub, and create `community`.
- Make real prints and real-camera captures. These are the evidence for print compensation, latency targets, and any comparison with QR.
- Apply the repository settings in `docs/REPOSITORY-SETTINGS.md` (branch and tag protection, private vulnerability reporting).
