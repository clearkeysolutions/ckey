# Implementation status against the Rendering, SDK, Distribution & Telemetry Specification

Maps each section of [the specification](SPECIFICATION-rendering-sdk-distribution-telemetry.md) to the repository as of `0.1.0-dev.59` (branch `scratch`).

**Status key:**

- **Done:** exists and tested here.
- **Partial:** some of it exists, or it exists but not in the specified form.
- **None:** not started.
- **Conflict:** current behaviour contradicts the specification and needs a decision or change.

Real-device, real-print and platform claims are only made where measured.

## Rendering (spec §2–§3)

| Item | Status | Where / notes |
|---|---|---|
| Canonical mathematical geometry, not SVG (§2.1) | **Partial** | Site geometry is computed in the core (`ScalableLayout`, `build_scalable_layout` in `src/ckey.cpp`). It is internal only, with no public geometry or path API. |
| Finder exclusions, safe gaps, no dead regions, fixed ray widths (§2.1, §11.3) | **Done** | Safe-area rule `dense_site_margin`. Measured ink gaps and dead-space checks in `tests/layout_tests.cpp` and `tests/scalable_tests.cpp`. |
| Separation: encoding / geometry / backends / display / scan (§2.1) | **Partial** | One library (`ckey_core`) holds everything. A scan-only build cannot omit the renderer yet. |
| Headless raster backend without an SVG interpreter (§2.2, §11.4) | **Conflict** | `ckey_render_scalable_print` builds SVG and rasterizes it with the reference parser (`src/svg_raster.h`). It needs a direct geometry → pixel path. |
| Raster policies: binary / coverage AA / hybrid (§2.2) | **Partial** | Binary (print) and supersampled coverage (test rasterizer) exist. No hybrid, and no policy benchmark yet. |
| Native RGBA / gray buffer API (§2.5) | **Partial** | Gray, binary print bitmap only. No RGBA, no alpha. |
| PNG export (§2.5) | **Partial** | 1-bit PNG writer in the CLI (`cli/print_output.h`), not in the core or SDKs. |
| SVG export (§2.3) | **Done** | All profiles. Grouped layers. |
| PDF vector export (§2.3–§2.4) | **None** | |
| `.ckey` interchange (§2.5) | **None** | PROPOSED in the specification. |
| Transparent output, alpha 0 when unpainted (§2) | **Conflict** | Every SVG paints a background rect. There is no transparent mode. |
| Print backend: physical size, minimum features, compensation (§2.4) | **Partial** | Device-grid print bitmaps at a given size and dpi. Profile 0.1 print layout. Simulated printer models (`tests/print_sim.h`). No measured printer/media profiles yet; the calibration sheet exists but no real prints have been measured. |
| Web canvas / WASM (§3) | **None** | |
| Swift package / `CKeyView` (§3) | **None** | Apps compile `src/ckey.cpp` directly. There is no `Package.swift`. |
| Android / .NET (§3) | **None** | |

## Styling and optical safety (spec §4)

| Item | Status | Where / notes |
|---|---|---|
| Colours, textures, images, materials (§4) | **Partial** | Profile 0 `ckey_style`: sheen, vignette, accents, bevel, depth, grain, logo, mark shapes. |
| Default black on white (§4) | **Conflict** | The current default is cream `#f4f1e8` on black `#050505`. Print output already uses black on paper. |
| Transparent mode needs host background to certify (§4, §11.5) | **None** | |
| Contrast not certified from WCAG alone; tie to a capture/print model (§4) | **Conflict** | `ckey_check_scalable_style` gates on WCAG 3:1 and reports "style OK". It also checks effect limits and the logo budget, but not a capture model. |
| Policy modes strict / adaptive / override (§4) | **Partial** | Strict only: the renderer refuses failing styles. No adaptive or override mode. |

## Research harness (spec §5)

| Item | Status | Where / notes |
|---|---|---|
| Generate → degrade → decode → measure | **Partial** | Render/rasterize/decode round trips (`tests/roundtrip_tests.cpp`). Full-frame simulated reader with rotation, keystone, blur, clutter and polarity (`tests/reader_pipeline_tests.cpp`). Print and camera simulator (`tests/print_sim.h`). |
| Metrics: payload recovery, bit errors, physical size | **Partial** | Recovery, per-class bit errors, and minimum printed size are measured ad hoc; none of it is a reproducible benchmark tool yet. |
| False-positive rate, latency distributions | **Partial** | Noise-rejection test only. Timings are ad hoc, not percentiles. |
| Comparison with QR and Data Matrix at equal payload and area | **None** | |
| The six research directions (§5.1–5.6) | **Partial** | (1) key-based self-calibration and version identification done. (2) camera-aware geometry: Profile 0.1 partly. (3) region-adaptive density: none. (4) print compensation: simulated only, found ineffective at 600 dpi. (5) alternative radial encoding: none. (6) distributed alignment references: none. |

## Scanner (spec §6)

| Item | Status | Where / notes |
|---|---|---|
| Detection separate from decode | **Done** | `ckey_find_borders_gray8` → `ckey_refine_quad_gray8` → `ckey_decode_*_gray8`. |
| Offline, no telemetry or server | **Done** | No network code exists. |
| Continuous frames, emit on first valid decode | **Done** | iOS reader, unbenchmarked. |
| Optional ML recognition | **None** | A small logistic candidate ranker only. |
| Latency target defined by device, size, profile and percentile | **None** | |
| Conformance vectors | **Partial** | Site-level FNV-1a hashes for P0, P0.1, P2 and P3, plus the RS vector. Not packaged as a standalone conformance suite. |

## Packaging and governance (spec §7–§8)

| Item | Status | Where / notes |
|---|---|---|
| SPM / AAR / npm / NuGet / CMake package / Homebrew (§7) | **None** | CMake builds the library and CLI, with no install or export package. |
| Signed, checksummed artifacts from tagged commits (§7) | **None** | |
| Branch model (§8) | **Partial** | `scratch` created (maintainer-only working branch). `community` and `Stable-Release` are being created by the maintainer. See [BRANCHES-AND-RELEASES.md](BRANCHES-AND-RELEASES.md). |
| Immutable version tags, deprecation titles (§8.1–8.2) | **None** | No tags exist yet. |
| Version counter preserves dev history (§8.1) | **Done** | `VERSION` is SemVer with a prerelease counter (`0.1.0-dev.N`). It is incremented every commit and checked by `tools/check_version.py`. |

## Telemetry (spec §9)

| Item | Status | Where / notes |
|---|---|---|
| Modes 0 / 1 / 2, invalid values rejected | **None** | |
| Local aggregate counters, no identifiers | **None** | |
| Ingestion service, batching, abuse controls | **None** | No CKey endpoint exists yet. Mode 1 transmission needs one. |

## Acceptance criteria (spec §11)

| # | Status | Notes |
|---|---|---|
| 1 Deterministic geometry across targets | **Partial** | Deterministic in the one C++ core; site sets are checked for libm-independence. There are no other targets yet. |
| 2 Deterministic raster | **Partial** | Deterministic, but through the SVG path. |
| 3 Exclusion, gap and ray-width invariants tested | **Done** | |
| 4 Headless without an SVG interpreter | **Conflict** | See above. |
| 5 Transparent compositing, no false certification | **None** | |
| 6 Scanability checks tied to a modelled acquisition | **Partial** | |
| 7 Native pixels in web, Swift, Android, .NET | **None** | |
| 8 Scanner independent of renderer and telemetry | **Partial** | The scanner is independent of telemetry, but it ships in the same library as the renderer. |
| 9 Native package ecosystems | **None** | |
| 10 Immutable tags, deprecation metadata | **None** | |
| 11–13 Telemetry | **None** | |
