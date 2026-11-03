# CKey living project context

**Current software version:** `0.1.0-dev.32`
**Current wire format:** `CKey Format 0.11.0` (format byte `0B`, locator profile byte `01`); text 0.12 / extended 0.13
**Branch:** `main`
**Context updated:** 2026-11-03 (finder geometry revision)

## Mission

CKey is an independent, monochrome optical encoding standard that must work without Viadere or any other service. Its defining geometry combines a rounded-square Cartesian field and corner finders with a polar interior containing radial rays, discrete dots, concentric rings, and connected arcs. The project must deliver mathematical correctness and camera-tested interoperability before capacity or speed claims.

## Decisions that currently govern implementation

* The repository's inherited MIT license is retained for now. Do not copy research implementations without auditing exact source and dependency licenses.
* Software versioning uses Semantic Versioning with prerelease identifiers during development. `VERSION`, the public API version string, release metadata, and this context must agree.
* The locator profile carries exactly 16 bytes. Its frame is magic/version/profile/length/payload/CRC-32C; the 26-byte frame is encoded with shortened systematic RS(77,26) over GF(256). ECC is a mathematical error-correcting code, not masking, XOR obscuring, or cosmetic transformation.
* Experimental Profile 3 is Format 0.10.0 / byte `0A`, profile `03`: it carries 1–2,048 bytes with ten byte-interleaved systematic RS(255,223) blocks. It uses a 116-unit field, a 232×232 Cartesian grid, 24 laterally dense polar rings, connected run arcs, and a 576-bearing ray alphabet with Gray-coded length and calibrated stroke-weight states. Its 2×2 Grid Key uses the identical module size and pitch as its Cartesian payload, while finder-stroke-only clearance preserves usable capacity around the corners. Its byte-codeword decoder and renderer are tested; image/mobile decode is not implemented and must not be claimed.
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^50`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Finder arcs (all profiles) now share the outer rounded-border corner centers and radius family, matching the concept frame style. Top-left remains split for orientation. This is a wire-visible geometry change (formats 0.11/0.12/0.13).
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.8.0 has four inner polar rings, a separate narrow annular dial band, an outer Cartesian matrix, and one thick corner arc at bottom-left, top-right, and bottom-right; the top-left corner arc is split by a one-module clear gap. The 6 × 6 corner envelopes are reserved to prevent finder/data collisions. Ring dots are a uniform 0.30 module diameter; the Ring Key arc has round ends and a four-slot visible length; data dial slots encode each bit using short and long rays; the codeword uses 51 parity bytes so 616 of 621 available sites carry RS-coded bits.
* The C ABI now decodes 8-bit grayscale images when given a caller-supplied cyclic quadrilateral: it tries all corner starts/windings, rectifies a projective mapping, verifies Grid/Ray/Ring calibration evidence, samples the Cartesian and polar payload sites, normalizes light/dark polarity, and runs RS plus frame/CRC validation. It decodes committed browser-rendered captures and a 500×500 crop from the user's iPhone camera preview of a laptop display. Full-frame localization, lens-distortion correction, and live-device verification remain incomplete. No QR-parity or sub-200 ms end-to-end performance claim is made; the aspirational 2,048-byte profile is not implemented. Erasure decoding is not implemented.

## Current repository state

Milestone 1 remains in progress: C++20 core, C ABI, locator encoder and byte-codeword decoder, ROI-assisted grayscale image decoder, deterministic geometry placement, monochrome SVG renderer, CLI encoder, versioned draft formats, research/license audit, independent RS reference, unit/optical image tests, and an experimental SwiftUI iPhone camera reader. The iOS app passes grayscale frames to the shared C ABI. Dev.25 eliminates fixed scale/offset ladders: the native core exposes an additive calibration-score API that measures permanent Grid/Ring/Ray key contrast and expected evidence under a proposed quadrilateral, and the reader uses a shrinking-step continuous geometry search from each detected border or rectangle before decoding. Dev.26 adds Format 0.9.0 Profile 2. Dev.31 adds Format 0.10.0 Profile 3: real 1–2,048 byte framing, ten interleaved RS(255,223) blocks, 232×232 Cartesian density, 24 laterally denser rings, connected payload arcs, and 576 calibrated multi-state rays. Dev.32 switches finder arcs to border-matched corner centers (same curvature family as the outer rounded frame). Its Cartesian placement is a deterministic two-dimensional permutation so the real codeword fills the full usable field. The 2×2 Grid Key uses the payload's exact module dimensions and pitch, the finder interior plus curved stroke is reserved, and payload remains within the structural-border guard. Profile 2 and Profile 3 have byte-codeword and SVG interoperability tests but are not supported by the grayscale/mobile image decoder; neither must be advertised as camera validated. The 16-byte locator remains the only physical-camera-tested profile (fixtures predate the border-matched finders and need regeneration). Calibration, RS, and CRC retain acceptance authority. This new Swift code needs an Xcode/iPhone build and quantitative distance, blur, damage, and noise testing. There are no third-party runtime dependencies in the C++ core.

Next engineering gates: (1) compile and validate the iPhone reader on physical devices; characterize localization and decode with held-out camera captures; (2) improve full-frame localization, image tolerances, and known-erasure correction; (3) add Kotlin and WASM/JS SDKs backed by the shared core; (4) build the browser lab and release/benchmark automation; (5) regenerate optical image fixtures for the border-matched finder geometry. Geometry feedback still open includes equal angular spacing across concentric rings and the intended number/placement of arc modules; changes must use a new format version. Update this context in the same change that moves any gate or changes a governing decision.

## Validation recorded for this version

* Prior compiler and core-test results are recorded in the changelog for their versions.
* C++ core tests updated for format byte `0B`, new CRC-32C/RS vector, and border-matched finder SVG geometry assertions.
* `python3 tools/rs_reference.py` points at `tests/vectors/locator-v11.json`.
* Optical image fixtures (`locator-v8-*.pgm`) still encode pre-dev.32 finder geometry and must be regenerated before image decode tests are expected to pass against newly rendered symbols.
* CMake/CTest and Xcode build/run remain pending physical-device validation in this environment.

## Research references and provenance

See [`spec/RESEARCH.md`](spec/RESEARCH.md). FHNW annular codes, CCTag, RuneTag, AprilTag, App Clip Code reverse engineering, HCC2D, PiCode, and IRIS inform comparison only. No external source code is incorporated.

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
