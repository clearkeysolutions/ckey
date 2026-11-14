# CKey living project context

**Current software version:** `0.1.0-build.120`
**Current wire formats:** `CKey Format 0.14.0` (byte `0E`, locator profile `01`), `0.25.0` (byte `19`, text profile `02`), and `0.26.0` (byte `1A`, extended profile `03`), plus `0.27.0` (byte `1B`, scalable profile `00`), plus `0.28.0` (byte `1C`, Profile 0.1 print layout), plus the opt-in byte-grouped placements `0.29.0` (byte `1D`) and `0.30.0` (byte `1E`), the experimental pixel layout `0.31.0` (byte `1F`), and the Arc layouts `0.32.0` (byte `20`) and `0.33.0` (byte `21`, byte-grouped; the SDKs' default from version 8 since build 114)
**Branch:** `scratch` (maintainer working branch; see docs/BRANCHES-AND-RELEASES.md). Cleared after the v0.1.0-build.76 tag; v0.1.0-build.83 is the first published release: it holds only the versioning files, this record and the documentation. From build 86 the source tree is back on `scratch` (restored from `Stable-Release` at `ede38f5`, by owner decision): changes are written and tested here, then promoted to `Stable-Release`. Version and release history: docs/RELEASE-AND-VERSIONING-MAP.md. Changelogs: CHANGELOG.md (builds), CHANGELOG-STABLE.md (releases; first published: v0.1.0-build.83), CHANGELOG-COMMUNITY.md (community merges).
**Context updated:** 2026-11-14

## Mission

CKey is an independent, monochrome optical encoding standard that must work without Viadere or any other service. Its defining geometry combines a rounded-square Cartesian field and corner finders with a polar interior containing radial rays, discrete dots, concentric rings, and connected arcs. The project must deliver mathematical correctness and camera-tested interoperability before capacity or speed claims.

## Decisions that currently govern implementation

* The repository's inherited MIT license is retained for now. Do not copy research implementations without auditing exact source and dependency licenses.
* Software versioning uses Semantic Versioning with prerelease identifiers during development. `VERSION`, the public API version string, release metadata, and this context must agree.
* The locator profile carries exactly 16 bytes. Its frame is magic/version/profile/length/payload/CRC-32C; the 26-byte frame is encoded with shortened systematic RS(77,26) over GF(256). ECC is a mathematical error-correcting code, not masking, XOR obscuring, or cosmetic transformation.
* Profile 2 Format 0.25.0 / byte `19`, profile `02`, carries 0–200 bytes under RS(255,210); legacy format bytes 9, 15, 18, 19, 22, 23 remain byte-codeword readable. Profile 3 Format 0.26.0 / byte `1A`, profile `03`, carries 1–2,048 bytes with ten byte-interleaved RS(255,k) blocks across seven capacity/ECC tiers from 256 to 2,048 bytes; tiered bytes 17, 20, 21, 24 and legacy bytes 10, 16 remain byte-codeword readable. Short frames use deterministic SplitMix32 visual fill under the same RS code. Both dense layouts (dev.49) derive Cartesian sites from a declared safe area — ink clearance from the frame, each finder arc including caps and concave side, the Grid Key, and the radial alphabet (P2 0.5 units everywhere; P3 1.5 frame/finders, 0.5 otherwise) — order every site (Cartesian, ring, dial/ray component) by a keyed bijective hash so the codeword spreads uniformly, and XOR every printed value with a specified SplitMix32 whitening stream. Whitening is optical modulation, not ECC. Fill sites carry whitening only: 590 of 2,630 sites in P2 and 22,245 of 42,645 in P3, which is capacity a future format could repurpose (larger modules or more parity). P3 uses a 116-unit field, 232×232 lattice, 34 rings from r=7.0 to 24.62, the Ring Key at 25.11, and a 576-bearing eight-state ray alphabet. Image decoders read only the current layouts; Profile 2/3 camera captures and device scans are still unvalidated.
* Profile 0, Format 0.27.0 (byte `1B`, profile `00`), is the scalable family. It is added alongside Profiles 1–3, which are unchanged and still emitted.
  * **Versions:** 31, sharing Profile 2's skeleton in the same 29-unit field (frame, finders, rings from r = 3.4, dial from r = 7.6). Version v has a fixed integer table of modules across (29 to 232), slots, rings, and Grid Key cell. The pitch is 29/G, about 7% finer per step; v11 has Profile 2's density and v31 has Profile 3's.
  * **Scaling:** every mark, key and clearance scales with the pitch.
  * **Capacity:** every site carries a codeword bit (no fill waste), protected by four Reed–Solomon levels (L/M/Q/H, about 7/14/22/30% per block). Capacity at level M rises from 27 to 4,619 bytes, with no step above 1.6×.
  * **Version identification:** the Grid, Ring and Ray Keys are drawn at each version's own module and slot pitch, so the decoder identifies the version from them. The frame's version byte, RS and CRC confirm.
* Profile 0.1, Format 0.28.0 (byte `1C`, profile `00`, layout `CKEY_LAYOUT_PRINT`), is a print-robust layout family alongside Profile 0. It keeps Profile 0's skeleton, version table, framing and ECC. Every mark and gap is at least about one module:
  * equal-arc ring slots;
  * 0.3p ring dots;
  * wider, fewer rays;
  * a compact 8-slot Ring Key.
  The decoder identifies the family from the keys. ABI 3 adds `layout` to `ckey_scalable_symbol` and to the P0 encode/decode/capacity calls.
* Rendering, SDK, distribution and telemetry (builds 61–68; docs/SPECIFICATION-rendering-sdk-distribution-telemetry.md, status in docs/IMPLEMENTATION-STATUS.md):
  * **Versions and tags:** versions are `0.1.0-build.N`; tags are `v0.1.0-build.N` on `Stable-Release`. `tools/bump_version.py` updates every versioned file.
  * **Defaults:** the default style is black on white; the concept look is `CKEY_PRESET_CONCEPT`. ABI 4 adds `ckey_style.policy` (strict, adaptive, override).
  * **Libraries:** the core is split into `ckey_codec`, `ckey_scan` (needs only the codec), `ckey_render`, `ckey_certify` (scan + render) and `ckey_telemetry` (independent). `src/ckey.cpp` is the amalgamation used by Xcode and the white-box tests.
  * **Canonical geometry:** `ckey_scalable_geometry`. Native raster, PNG and PDF are drawn from it. SVG output is kept consistent with it by a pixel test.
  * **Certification:** a modelled scan with the real decoder. A transparent symbol needs a backdrop to be certified.
  * **Telemetry:** modes 0/1/2, default 1. Endpoint `https://openckey.org/api/telemetry/v1/batch`. The core never does networking; SDKs send.
  * **SDKs:** Swift (`Package.swift`), npm `ckey`, Maven `org.openckey:ckey`, NuGet `CKey`. The Swift, wasm, Kotlin and C# builds are verified only in CI.
  * **openckey.org:** operated from a separate, proprietary repository. It provides telemetry ingestion, `/text/{id}` and `/sq/{id}` destinations, the hosted renderer and package downloads. The prototype `services/openckey` was removed from this repository in build 71.
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^50`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.14.0 uses one broad inboard arc per rounded corner, inside a continuous rounded-square border; each arc's mouth faces the central polar field. Only the top-left arc is split on its diagonal for orientation. The 6 × 6 corner envelopes are reserved to prevent finder/data collisions. Ring dots are a uniform 0.30 module diameter; the Ring Key arc has round ends and a four-slot visible length; data dial slots encode each bit using short and long rays; the codeword uses 51 parity bytes so 616 of 621 available sites carry RS-coded bits.
* The C ABI now decodes 8-bit grayscale images when given a caller-supplied cyclic quadrilateral: it tries all corner starts/windings, rectifies a projective mapping, verifies Grid/Ray/Ring calibration evidence, samples the Cartesian and polar payload sites, normalizes light/dark polarity, and runs RS plus frame/CRC validation. It decodes committed browser-rendered captures and a 500×500 crop from the user's iPhone camera preview of a laptop display. Full-frame localization, lens-distortion correction, and live-device verification remain incomplete. No QR-parity or sub-200 ms end-to-end performance claim is made; the 2,048-byte profile is implemented but its physical-camera capacity is not characterized. Erasure decoding is implemented for Profile 0 from build 86 (see "Build 86: the repair pass" below).

## Current repository state

Milestone 1 remains in progress: C++20 core, C ABI, locator encoder and byte-codeword decoder, projective grayscale decoders for all three profiles, deterministic geometry placement, monochrome SVG renderer, CLI encoder, versioned draft formats, research/license audit, independent RS reference, unit/optical image tests, and a SwiftUI iPhone camera reader. The reader calls the shared C ABI for every profile. Dev.25 introduced a continuous key-based geometry search rather than fixed scale ladders. Dev.31 added Profile 3's 1–2,048 byte framing, ten interleaved RS(255,223) blocks, 232×232 Cartesian density, connected payload arcs, and 576 calibrated multi-state rays. Dev.36 establishes the approved continuous rounded border plus one inboard arc per corner in every profile, whose mouth faces the polar field; only top-left is split. Dev.42 adds deterministic visual fill for Profile 2 and seven Profile 3 capacity/ECC tiers. Dev.43 versions Profile 2's frame moat and Profile 3's 34-ring polar map so data cannot visually spill into the border, replaces generic rectangle detection with physical-wide-camera border components plus Grid/Ring/Ray/finder key fitting, and adds a reproducible synthetic candidate ranker. That ranker only orders measured candidates; payload authority remains each profile's framing, Reed–Solomon, and CRC checks. Profile 1 has committed camera-capture fixtures. Before dev.49, Profiles 2 and 3 could not decode even their own clean renders (sampler support wider than their marks); dev.49 fixes this, fixes the dead-space and safe-area failures of the prior dense layouts, and adds CI round trips through an SVG rasterizer. Profiles 2 and 3 remain unvalidated on physical cameras; no distance, blur/noise resilience, or latency target is claimed. The Xcode builds and physical iPhone validation are still required. There are no third-party runtime dependencies in the C++ core.

Next engineering gates: (1) compile and validate the iPhone reader on physical devices; characterize localization and decode with held-out camera captures; (2) improve full-frame localization, image tolerances, and known-erasure correction; (3) add Kotlin and WASM/JS SDKs backed by the shared core; (4) build the browser lab and release/benchmark automation. Geometry feedback still open includes equal angular spacing across concentric rings and the intended number/placement of arc modules; changes must use a new format version. Update this context in the same change that moves any gate or changes a governing decision.

## Validation recorded for this version

* Dev.49, run in this environment: CMake Release build with `-Wall -Wextra` is warning-free; all six CTest suites pass in Release (checks are always on) and in Debug with ASan/UBSan.
* Round trips through `tests/svg_raster.h` cover all profiles: render, rasterize, then decode with all-zero, all-`FF`, random, short and tier payloads. The suite also covers rotated corner order, reversed winding, inverted polarity, a 6% keystone and a 3×3 box blur. Densities are P1 10, P2 14 and P3 16 px/unit.
* The rasterizer agrees with Chromium on the same SVGs to a mean absolute difference of 1.2–1.5 grey levels.
* Measured clean-render floors with an exact quad, in Chromium: P1 5–6, P2 10, P3 (2,048-byte tier) 11 px/unit.
* P1 on 40 random renders: dev.48 failed 40/40 at 5 px/unit and 19/40 at 6–8 px/unit. Dev.49 fails 0/40 at 5, 6, 8 and 12 px/unit.
* The committed P1 camera fixtures still decode. A narrower P1 sampling support broke the iPhone camera-preview fixture, so P1 keeps its original support.
* `tests/layout_tests.cpp` asserts the normative site counts and that no lattice site lies within 1e-6 of a clearance boundary. The closest sites are P2 0.018 and P3 0.0003 units away.
* The same suite asserts that every quadrant and site kind carries a proportional share of the codeword (within 0.05 for quadrants, 0.03 for P3 site kinds and 0.06 for P2 site kinds). It also checks the rendered gaps. P2: frame 0.775, finders 0.654, grid 0.725. P3: frame 1.812, finders 1.601, grid 0.688.
* The largest payload-free distance in the field is ≤ 1.10 units in P2, at the Ray Key clearance, and ≤ 1.32 in P3, at ray tips.
* Dev.50: rule-7 review separation is recorded here. A second reviewer (not the dev.49 author) read and ran dev.49 against the v0.25/v0.26 specifications. Verified findings, all resolved in dev.50:
  * Blurred isolated P3 ring dots all decoded as 0. Fixed with ring-context thresholds. P3 now decodes 3/3 at 10–16 px/unit through two 3×3 blurs, and P2 survives one blur at 10 px/unit and two at 12.
  * The two-means cut bound was too tight for P3 width coverage.
  * The P2 calibration-score empty reference was a payload site.
  * The specifications lacked render geometry, Gray component order, an explicit SplitMix32 definition, and a known-answer vector.
  * Tests did not check the Cartesian-to-radial gap or the documented boundary slack, and blur was tested at only one density.
  * The reviewer confirmed spec-vs-code constants, site counts, determinism (site set, ordering, P1 binning), all other empty references, renderer/decoder consistency, and polarity and ambiguity handling.
* Unverified suspicion: the review raised P2 isolated dots under heavier blur. P2 now fails only at two blurs and 10 px/unit.
* These checks use synthetic renders only and do not establish physical-camera robustness.
* `python3 tools/check_version.py`: software and current wire-format nodes are synchronized for dev.49. It also rejects a stale "current Format" claim in this file and missing README format links.
* Earlier records: C++ core tests pass across error weights 1–25, including 32 distinct 25-byte-error patterns and a 26-error rejection vector. Independent review corrected 1,000 randomized 25-byte-error patterns and 10,000 eight-symbol-error patterns. `tools/rs_reference.py` independently reproduces the committed RS vector. Static and hidden-visibility shared builds exported the public `ckey_*` ABI.
* Dev.51: the reader's 1080p capture could not supply P3's ~1,280 px decode floor, so it now prefers 4K, and its border search samples about 960 × 540 points at any capture size. The Swift changes were reviewed by inspection only; no Swift toolchain or Xcode is available here.
* Dev.53: on-device testing showed P1 never scanning and P2 rarely scanning. The cause was the reader's principal-axis border orientation, which cannot orient a square and was measured 11–45° off.
  * Localization moved into the core (`ckey_find_borders_gray8`, `ckey_refine_quad_gray8`), and the Swift path now calls it.
  * `ckey_simulated_reader` decodes 80/80 synthetic full-frame scenes covering P1–P3, rotation up to 33°, keystone, blur, clutter, and both polarities.
  * The ranker weights are now embedded in the core's `border_rank`, and `ios/CKeyReader/LocatorRanker.swift` is no longer called. Regenerating the ranker requires updating `border_rank`.
* Dev.56 (Profile 0) results:
  * `ckey_scalable_profile0` renders, rasterizes and decodes all 31 versions with random, all-zero and all-`FF` payloads at 6 px/module, and at 8 px/module after a 3×3 blur. The keys identify the version every time. The suite also checks the normative tables, safe-area clearances for every version, byte-level ECC at all four levels, and two conformance hashes.
  * `ckey_simulated_reader` decodes P0 v1, v2, v11, v20 and v31 in full 1080p/4K camera frames with clutter, rotation, keystone, blur and inverted polarity; overall it is 144/144.
  * P0 localization added an edge-based frame objective with coarse-to-fine sample spacing (v31 corners to 0.06–0.22 modules), skeleton ranking of border candidates, and iterated per-corner refinement.
  * The iOS reader tries P0 first and the CKey Renderer can generate P0 (version auto or 1–31, level L/M/Q/H). Their Swift changes are unverified until built in Xcode.
* Dev.57 design system:
  * **Scope:** `ckey_style` styling for Profile 0 (shapes of payload marks, colours, materials, logo). The frame, finders and keys stay fixed, as the maintainer specified.
  * **Skeleton:** the P0 skeleton is now style-invariant and symmetric, with orientation from the keys.
  * **Tests:** all 16 shape combinations decode; the checker's refusals are tested; and the largest logo accepted at each level decodes.
  * **Browser:** concept-styled renders in Chromium decode exactly, sharp and blurred.
  * **Logo limit:** logos fit the empty centre (radius about 3.2). A larger logo would need byte-grouped placement, a wire-format change.
  * **Unbuilt:** the Mac Design panel has not been compiled yet.
* Dev.58 printing work:
  * **Simulator:** a print simulator (`tests/print_sim.h`) models binary device dots, ink spread, paper blur, then a camera at a given resolution.
  * **Baseline P0 findings:** printing dark marks on paper beats the cream-on-black palette by about 35% on plain inkjet. Ink-spread compensation is ineffective at 600 dpi, because the spread is about one dot. At the limit, ring dots fail first.
  * **P0.1 result:** the same payload decodes at about half P0's width, for example 120 B at 0.32 in versus 0.68 in on the old-inkjet model. A regression test locks this in: at 0.42 in, P0 fails and P0.1 decodes.
  * **Print output:** device-grid 1-bit output through `ckey_render_scalable_print`, `ckey encode-scalable --png`, `ckey print-sheet` (calibration ladder) and the Renderer's Export Print PNG.
  * **Still to do:** the simulation parameters are estimates; real prints from the calibration sheet are pending.
* Xcode build/run and live camera performance remain pending physical-device validation; they are unavailable in this environment.

## Research references and provenance

See [`spec/RESEARCH.md`](spec/RESEARCH.md). FHNW annular codes, CCTag, RuneTag, AprilTag, App Clip Code reverse engineering, HCC2D, PiCode, and IRIS inform comparison only. No external source code is incorporated.

## Build 86: the repair pass (2026-11-10)

Build 86 implements `Pretest-observations-for-repair.md` (the repair notes from the CKTEST-RH-001 campaign, `clearkeysolutions/key-test`). That file is deleted; this section is its record. What changed is in `CHANGELOG.md`.

**Owner decisions taken for this build (2026-11-10):**
* The source tree returns to `scratch`.
* Byte-grouped Formats 0.29/0.30 and levels U and X ship in both the reader and the encoder/renderer.
* The print layout (P0.1) is **not** the default; it is the print version. The default stays Format 0.27, and the encoder already picks the lowest version that fits. (Superseded for versions 8–31 at build 114: see "Builds 110–115" below.)
* The SDK scanner takes control of the camera by default; the snapping brackets are an optional part of the SDK.
* The iOS app (`clearkeysolutions/ckey-web`, `apps/ios`) is re-pinned to this build and its scanning code replaced by the SDK camera layer.

**Result, honestly.** On 6,000 structured campaign cases (the first 300 of each stratum, same frames for both builds) the reader decoded **76.0%** of physically recoverable symbols, against 64.4% for build 85, and 59.1% against 50.0% of all valid symbols. Standard layout 70.5% (was 60.8%); print layout 81.5% (was 68.2%). 71 cases that build 85 read are now missed; they were not investigated. The full table is in `CHANGELOG.md`. With the later ring work (physical ring model, lens from the rings, per-ring kernel learning; see `docs/research/reading-through-blur.md`) the same 6,000 cases give 76.5% of recoverable symbols (standard layout 71.5%, print 81.6%) at a median of 105 ms per frame.

No wrong payload. The reader is still far from the 98–100% of recoverable frames the owner expects. Why, from the per-stage measurements made during this build:

1. **Ring dots in the standard layout are the main limit.** Their dots have a radius of 0.2 module (0.4 across) and sit 0.3 module apart on the inner ring, so neighbouring printed dots overlap and one unprinted slot between two printed ones is a gap of about 0.2 module: ring bits need about three times the resolution of the modules. (An earlier version of this record, and the first ring model in the reader, wrongly took 0.2 as the diameter.) In frames that fail with the true corners given, modules typically read with 0% bit errors while ring bits read 10–30% wrong, and with scattered placement almost every byte holds a ring bit. On the 2,000-case subset the standard layout read 85% of recoverable frames under 0.2 module of blur, 34% at 0.2–0.4 and 0% above; the print layout read 88%, 81% and 16%. The harness calls a frame recoverable from module-level physics, which these dots do not meet. A sequence detector for the rings (fit a blur model, find the most likely bit run) is in the reader and rescues some frames, but on the hard set it does not lower the mean ring error: the model does not match real frames well enough yet (sharpening, tone curve, neighbouring rings, registration). This needs either a better model or a format whose rings are not this fine. Grouped placement stops ring errors from spoiling every byte but does not make the dots readable.
2. **Curved surfaces.** The perspective stratum (S04) bends the symbol on a cylinder. Done in part after the first build 86 measurement (see "Bent surfaces" below): 20 of the 45 recoverable bent symbols in the 6,000-case set now decode, against none.
3. **Heavy blur** (0.4 module and more) fails for both layouts.
4. **Corner fit.** Much improved (direct frame-edge fit, lens term), but frames remain where the true corners decode and the reader's do not.

**Reader speed regressed.** On the same run the median reader time per frame rose from 41 ms to 88 ms and the 95th percentile from 187 ms to 536 ms (frames that decode: 32 → 49 ms median; frames that do not: 77 → 261 ms). The campaign's host P95 was already above the 200 ms gate. Failing frames cost most (several strategies and both placements are tried per candidate). Not yet measured on a device.

**Bent surfaces (added 2026-11-10, after the ring work).** `ckey_decode_scalable_gray8` tries a cylindrical surface when the flat attempts fail and the frame's sides curve (`SurfaceModel`, `versions_from_lattice`, `fit_frame_curves`, `surface_from_marks`, `surface_pose`, `corners_from_bent_frame`). On the 6,000 cases: recoverable symbols decoded 76.5% → 77.2%, S04 70.0% → 81.1%, no case lost, no wrong payload. What it does not do:
* **Only cylinders.** A crumpled or doubly curved surface is not modelled.
* **Strong bends.** Beyond a half-angle of about 0.4 radian (a label wrapped more than about 45 degrees round) the fit usually fails.
* **Low versions under a strong bend.** Versions 1 to 12 have too few modules for the lattice-period estimate, so each is tried in turn within a work budget; the budget runs out before the higher of them on some frames.
* **Bend plus lens distortion** in one frame is not fitted (the bent pass assumes no radial distortion).
* **Bends that do not curve the frame.** The search runs only when a side departs from its chord by 0.06 unit or more. A cylinder whose axis is square to the direction of tilt moves the modules along the sides without curving them, and is skipped.
* **Cost.** Frames that fail with a curved frame pay for the search: the 95th percentile on failing frames rose from about 0.6 s to about 1.0 s.
* **Pixel-domain ring detection** (a second observation per ring slot, averaging across the stroke) was tried and gave nothing; the rings' remaining failures are of registration and convergence, not of signal.

**Repair-note items not done, or only partly done:**
* 1.1/1.6 moiré pre-filter keyed on a detected periodic pattern: not done (area-averaged search and local levels are in).
* 1.6 item 4 / 2.4 multi-frame soft combining: not done. Each frame is still decoded on its own.
* 1.4/1.7 ring-ellipse-constrained homography and direct image alignment: not done; the frame-edge fit, lens estimate and lattice registration were done instead.
* 1.10.2 partial crops: the decoder now reads a symbol partly out of frame when given its corners (missing sites become erasures), and the scan reports `CKEY_SCAN_FLAG_SYMBOL_CUT` so the SDK can say "move back"; the border search itself still requires the whole border in view.
* 1.10.3 finder loss: no dedicated fallback. Key and skeleton fits now tolerate missing samples.
* 1.10.1 lens: `k1` is estimated from the frame, or supplied to `ckey_scan_frame_gray8`; strong ultrawide distortion beyond about −0.15 (focal-normalised) is not handled by the supplied-`k1` path.
* 1.10.8 memory: not measured for the reader alone.
* 2 (own the camera): done for Swift, uncompiled. Kotlin/CameraX camera layer: not done. The core's advice policy is simple and untested on devices.
* 3.3 logos over data: the design-system checker still limits logos to the empty centre; it does not yet compute the erasure margin for a larger logo on a grouped symbol.
* 3.5 inner per-region code: not done, as the notes recommend.
* 4 QR/Data Matrix/Aztec baseline and the harness items (per-case timeout, stall alarm): key-test work, not here.
* The full campaign re-run (structured 1M, adaptive pool, holdout) has not been run on this build. Only the 6,000-case comparison above and the 2,000-case development subset were run, locally.

**Not compiled or run here:** all Swift (SDK camera layer, reference reader, Renderer app, the iOS app in ckey-web), the .NET SDK, the wasm build, the Android instrumented tests.

**Next:** build the Swift package and the iOS app in Xcode and field-test them; run the campaign's `./ckt build-candidate` and `./ckt regress` on the server with the locked design; decide what to do about the standard layout's ring dots (item 1) before more reader work.

## Builds 110–115: Arc and the automatic layout (2026-11-14)

This addresses item 1 of the build 86 record (the Standard layout's inner rings). Details are in `CHANGELOG.md`.

* **Formats 0.32 and 0.33, "Arc" (build 110).** Format 0.27 gives every ring the dial's slot count D at equal angles, so inner-ring marks sit up to twice as close as the outermost ring's (v21: 0.074 units apart on the innermost ring against 0.149 on the outermost; module pitch 0.25). Arc gives ring k 8·⌊D·r_k/r_outer/8⌋ slots; the outermost keeps D. The Ring Key starts at 6:00 instead of 3:00 so the version fit tells the families apart. 0.32 is scattered (`CKEY_LAYOUT_STANDARD_ARC` 5, web `'arc'`, Swift `.standardArc`, Android `STANDARD_ARC`); 0.33 is byte-grouped (`CKEY_LAYOUT_STANDARD_ARC_GROUPED` 6, `'arc-grouped'`, `.standardArcGrouped`, `STANDARD_ARC_GROUPED`). Capacity at M is about 10–12% lower for large versions (v21: 959 against 1,085 bytes; v31: 4,100 against 4,619) and more for small ones (v1: 21 against 27). Readers before build 110 cannot read Arc.
* **Why.** The owner's opt-in scan reports (Open CKey Scanner, per-site heatmap) showed Standard's ring errors falling steadily with radius, about 21% inside r = 4.3 to about 1% on the outermost rings, grid 0.1–0.4%. With scattered placement 4% wrong sites spoiled 26–30% of bytes; the worst Reed–Solomon block had 77–86 bad bytes against 35 correctable.
* **Automatic layout, the default (build 114).** `CKEY_LAYOUT_AUTO` = 255: Format 0.27 for versions 1–7, Format 0.33 for 8–31; with version 0, the smallest version that fits under that rule. The default in Swift (`encode(layout: nil)`), web (`'auto'` or omitted), Android (`Layout.AUTO`) and the CLI (`--layout auto`); opt-in in the C API. Naming a layout gives exactly that layout. Default-made codes from v8 need readers from build 110. The .NET SDK has neither Arc nor Auto yet (default Standard). Evidence: side by side on the owner's iPhone (same payload, reader build 113), Arc v22 at 392–438 px and Standard v21 at 442–465 px both read in a median of about 120 ms; in the frames that read, Arc had about a third of Standard's wrong sites (median 43 against 96) and a fifth of its inner-ring errors (0.3–1.9% against 3–9%), at a smaller size. Screen model (lattice at 6 cells per module, blur σ 1 px, 3% noise), smallest width read by the full reader: 300 B Standard 246 / Arc 210 px; 900 B 400 / 329; 2,000 B 590 / 507.
* **Reader.** Build 111 judges the Grid, Ring and Ray Keys each against their own blanks, so small or blurred codes whose data read fine no longer fail the version-key gate. Build 115's corner refinement always compares its three starting outline sizes, so the fast (live) scan reads large dense codes: smallest fast-scan width at 2,000 B, Standard 685 → 590 px, Arc 982 → 531 px.
* **Still open.** One owner's iPhone is the only field evidence; no community field test of Arc yet. The format specifications 0.32/0.33 still say encoders emit them "only when asked", which was true at build 110. Swift and Android were not compiled here.

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
