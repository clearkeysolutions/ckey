# CKey living project context

**Current software version:** `0.1.0-dev.41`
**Current wire formats:** `CKey Format 0.14.0` (byte `0E`, locator profile `01`), `0.15.0` (byte `0F`, text profile `02`), and `0.16.0` (byte `10`, extended profile `03`)
**Branch:** `main`
**Context updated:** 2026-11-03

## Mission

CKey is an independent, monochrome optical encoding standard that must work without Viadere or any other service. Its defining geometry combines a rounded-square Cartesian field and corner finders with a polar interior containing radial rays, discrete dots, concentric rings, and connected arcs. The project must deliver mathematical correctness and camera-tested interoperability before capacity or speed claims.

## Decisions that currently govern implementation

* The repository's inherited MIT license is retained for now. Do not copy research implementations without auditing exact source and dependency licenses.
* Software versioning uses Semantic Versioning with prerelease identifiers during development. `VERSION`, the public API version string, release metadata, and this context must agree.
* The locator profile carries exactly 16 bytes. Its frame is magic/version/profile/length/payload/CRC-32C; the 26-byte frame is encoded with shortened systematic RS(77,26) over GF(256). ECC is a mathematical error-correcting code, not masking, XOR obscuring, or cosmetic transformation.
* Experimental Profile 3 is Format 0.10.0 / byte `0A`, profile `03`: it carries 1–2,048 bytes with ten byte-interleaved systematic RS(255,223) blocks. It uses a 116-unit field, a 232×232 Cartesian grid, 24 laterally dense polar rings, connected run arcs, and a 576-bearing ray alphabet with Gray-coded length and calibrated stroke-weight states. Its 2×2 Grid Key uses the identical module size and pitch as its Cartesian payload, while finder-stroke-only clearance preserves usable capacity around the corners. Its byte-codeword decoder and renderer are tested; image/mobile decode is not implemented and must not be claimed.
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^50`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.14.0 uses one broad inboard arc per rounded corner, inside a continuous rounded-square border; each arc's mouth faces the central polar field. Only the top-left arc is split on its diagonal for orientation. The 6 × 6 corner envelopes are reserved to prevent finder/data collisions. Ring dots are a uniform 0.30 module diameter; the Ring Key arc has round ends and a four-slot visible length; data dial slots encode each bit using short and long rays; the codeword uses 51 parity bytes so 616 of 621 available sites carry RS-coded bits.
* The C ABI now decodes 8-bit grayscale images when given a caller-supplied cyclic quadrilateral: it tries all corner starts/windings, rectifies a projective mapping, verifies Grid/Ray/Ring calibration evidence, samples the Cartesian and polar payload sites, normalizes light/dark polarity, and runs RS plus frame/CRC validation. It decodes committed browser-rendered captures and a 500×500 crop from the user's iPhone camera preview of a laptop display. Full-frame localization, lens-distortion correction, and live-device verification remain incomplete. No QR-parity or sub-200 ms end-to-end performance claim is made; the aspirational 2,048-byte profile is not implemented. Erasure decoding is not implemented.

## Current repository state

Milestone 1 remains in progress: C++20 core, C ABI, locator encoder and byte-codeword decoder, projective grayscale decoders for all three profiles, deterministic geometry placement, monochrome SVG renderer, CLI encoder, versioned draft formats, research/license audit, independent RS reference, unit/optical image tests, and a SwiftUI iPhone camera reader. The reader calls the shared C ABI for every profile. Dev.25 introduced a continuous key-based geometry search rather than fixed scale ladders. Dev.31 adds Profile 3's 1–2,048 byte framing, ten interleaved RS(255,223) blocks, 232×232 Cartesian density, 24 laterally denser rings, connected payload arcs, and 576 calibrated multi-state rays. Dev.36 establishes the approved continuous rounded border plus one inboard arc per corner in every profile, whose mouth faces the polar field; only top-left is split. It versions the identical finder semantic as Formats 0.14 (locator), 0.15 (text), and 0.16 (extended), while retaining decode compatibility for legacy Profile 2/3 frames. Dev.38 adds Profile 2/3 projective grayscale samplers and a macOS visual stress laboratory; both applications call the shared C++ core. Dev.41 reorders live scanning to try measured quads on all profiles first, uses Vision on alternating sampled frames as fallback, and limits geometry refinement to one leading candidate. Detection and payload-decode measurements are reported separately. Payload authority remains each profile's framing, Reed–Solomon, and CRC checks. Profile 1 has committed camera-capture fixtures. Profiles 2 and 3 now have experimental projective image decoders and scanner paths but remain unvalidated on physical cameras; no distance, blur/noise resilience, or latency target is claimed. The Xcode builds and physical iPhone validation are still required. There are no third-party runtime dependencies in the C++ core.

Next engineering gates: (1) compile and validate the iPhone reader on physical devices; characterize localization and decode with held-out camera captures; (2) improve full-frame localization, image tolerances, and known-erasure correction; (3) add Kotlin and WASM/JS SDKs backed by the shared core; (4) build the browser lab and release/benchmark automation. Geometry feedback still open includes equal angular spacing across concentric rings and the intended number/placement of arc modules; changes must use a new format version. Update this context in the same change that moves any gate or changes a governing decision.

## Validation recorded for this version

* Prior compiler and core-test results are recorded in the changelog for their versions.
* C++ core tests passed across error weights 1–25, including 32 distinct 25-byte-error patterns and a 26-error rejection vector. The SVG regression tests check uniform 0.30-module dot radii, the round-capped four-slot Ring Key arc, four finder arc paths, and all 69 dial data rays. Independent review corrected 1,000 randomized 25-byte-error patterns.
* `python3 tools/check_version.py`: software version nodes synchronized for dev.26.
* `python3 tools/rs_reference.py`: independently generated RS parity matched the committed vector. `ckey_image_tests` decodes the 670×670 browser-rendered image, the user target, the close and far camera-preview crops to payload `00 01 … 0F`; rotated order, inverted polarity, and invalid degenerate quadrilaterals are also covered. Independent geometry/spec reviews confirmed the format-0.8 single-band finder layout and its one-module top-left gap, plus the format-0.7 uniform dots, four-slot curved arc length, and all 69 dial bits.
* Format 0.8.0 updates the format byte, so the 16-byte example fixture has a new CRC-32C/RS vector; the Reed–Solomon parameters and payload size are unchanged.
* CLI generated the format-8 example SVG; Python XML parser accepted it.
* Static and hidden-visibility shared builds passed; shared build exports the public `ckey_*` ABI, including the new grayscale image decoder. Render-scale boundary checks passed.
* CMake/CTest and Xcode build/run are unavailable in this environment; live camera performance and Vision localization remain pending physical-device validation.
* Independent ECC review ran 10,000 randomized eight-symbol-error patterns through the C ABI; all corrected to the original payload. Review identified shared-library visibility and render-scale bounds, addressed in dev.2.

## Research references and provenance

See [`spec/RESEARCH.md`](spec/RESEARCH.md). FHNW annular codes, CCTag, RuneTag, AprilTag, App Clip Code reverse engineering, HCC2D, PiCode, and IRIS inform comparison only. No external source code is incorporated.

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
