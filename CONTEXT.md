# CKey living project context

**Current software version:** `0.1.0-build.65`
**Current wire formats:** `CKey Format 0.14.0` (byte `0E`, locator profile `01`), `0.25.0` (byte `19`, text profile `02`), and `0.26.0` (byte `1A`, extended profile `03`), plus `0.27.0` (byte `1B`, scalable profile `00`), plus `0.28.0` (byte `1C`, Profile 0.1 print layout)
**Branch:** `scratch` (maintainer working branch; see docs/BRANCHES-AND-RELEASES.md). Version and release history: docs/RELEASE-AND-VERSIONING-MAP.md. Changelogs: CHANGELOG.md (builds), CHANGELOG-STABLE.md (releases, none yet), CHANGELOG-COMMUNITY.md (community merges).
**Context updated:** 2026-11-04

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
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^50`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.14.0 uses one broad inboard arc per rounded corner, inside a continuous rounded-square border; each arc's mouth faces the central polar field. Only the top-left arc is split on its diagonal for orientation. The 6 × 6 corner envelopes are reserved to prevent finder/data collisions. Ring dots are a uniform 0.30 module diameter; the Ring Key arc has round ends and a four-slot visible length; data dial slots encode each bit using short and long rays; the codeword uses 51 parity bytes so 616 of 621 available sites carry RS-coded bits.
* The C ABI now decodes 8-bit grayscale images when given a caller-supplied cyclic quadrilateral: it tries all corner starts/windings, rectifies a projective mapping, verifies Grid/Ray/Ring calibration evidence, samples the Cartesian and polar payload sites, normalizes light/dark polarity, and runs RS plus frame/CRC validation. It decodes committed browser-rendered captures and a 500×500 crop from the user's iPhone camera preview of a laptop display. Full-frame localization, lens-distortion correction, and live-device verification remain incomplete. No QR-parity or sub-200 ms end-to-end performance claim is made; the 2,048-byte profile is implemented but its physical-camera capacity is not characterized. Erasure decoding is not implemented.

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

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
