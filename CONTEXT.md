# CKey living project context

**Current software version:** `0.1.0-dev.8`
**Current wire format:** `CKey Format 0.6.0` (format byte `06`, locator profile byte `01`)
**Branch:** `main`
**Context updated:** 2026-11-03

## Mission

CKey is an independent, monochrome optical encoding standard that must work without Viadere or any other service. Its defining geometry combines a rounded-square Cartesian field and corner finders with a polar interior containing radial rays, discrete dots, concentric rings, and connected arcs. The project must deliver mathematical correctness and camera-tested interoperability before capacity or speed claims.

## Decisions that currently govern implementation

* The repository's inherited MIT license is retained for now. Do not copy research implementations without auditing exact source and dependency licenses.
* Software versioning uses Semantic Versioning with prerelease identifiers during development. `VERSION`, the public API version string, release metadata, and this context must agree.
* The locator profile carries exactly 16 bytes. Its frame is magic/version/profile/length/payload/CRC-32C; the 26-byte frame is encoded with shortened systematic RS(77,26) over GF(256). ECC is a mathematical error-correcting code, not masking, XOR obscuring, or cosmetic transformation.
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^50`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.6.0 has four inner polar rings, a separate narrow annular dial band, an outer Cartesian matrix, one thick corner arc at three corners and a matching top-left orientation arc split by a one-module clear gap. The 6 × 6 corner envelopes are reserved to prevent finder/data collisions. Ring dots are 72% of their angular-slot chord; the Ring Key arc matches one dot in width and four slots in length; the codeword uses 51 parity bytes so 616 of 621 available sites carry RS-coded bits.
* No claim that camera decoding, perspective recovery, sub-200 ms latency, or the aspirational 2,048-byte profile is complete or achieved. Current code encodes/renders SVG and the C ABI decodes byte codewords with up to 25 unknown byte-error corrections. Tests cover framing, CRC, an independent RS parity vector, 32 full-bound 25-byte correction patterns, and deterministic SVG/encoding. Erasure decoding is not implemented.

## Current repository state

Milestone 1 is in progress: C++20 core, C ABI, locator encoder and byte-codeword decoder, deterministic geometry placement, target-aligned monochrome SVG renderer, CLI, versioned draft formats, research/license audit, independent RS reference, and unit tests. Format 0.6.0 places four rings centrally and all ray marks inside a 1.9-unit annulus, with the square matrix outside. It uses one thick quarter-arc at bottom-left, top-right, and bottom-right, plus the same arc split by a one-module clear gap at top-left, with 6 × 6 reserved corner envelopes, slot-proportional, closely spaced ring dots, and 616 RS-coded sites; inner rings use radii 3.6, 4.4, 5.2, and 6.0, leaving a larger central clear area. The Grid Key is a 2 × 2 dark block; the ray key is confined to the dial annulus. Placement is spread across angular sectors and all five polar layers; the updated mask contains 284 Cartesian and 337 polar sites; only five eligible sites remain unassigned. This remains a binary profile and is not the supplied 13-state image decoder. Direct C++20 compiler build and tests passed in this environment; CMake is not installed, so CMake/CTest was not run. There are no third-party runtime dependencies in the core.

Next engineering gates: (1) implement RS erasure correction and expose decode in a CLI; (2) implement image localization, normalization, and perspective correction with held-out real camera captures; (3) add native and WASM/JS SDKs backed by the shared core; (4) build the browser lab and release/benchmark automation. Update this context in the same change that moves any gate or changes a governing decision.

## Validation recorded for this version

* `g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic`: core compiled without warnings.
* C++ core tests passed across error weights 1–25, including 32 distinct 25-byte-error patterns and a 26-error rejection vector. The SVG renderer explicitly disables inherited strokes on dot circles; regression test checks the formula-sized dots and equal-width four-slot Ring Key arc. Independent review corrected 1,000 randomized 25-byte-error patterns.
* `python3 tools/check_version.py`: software version nodes synchronized.
* `python3 tools/rs_reference.py`: independently generated RS parity matched the committed vector.
* CLI generated the format-6 example SVG; Python XML parser accepted it.
* Static and hidden-visibility shared builds passed; shared build exported all five public `ckey_*` ABI functions. Render-scale boundary checks passed.
* CMake/CTest not run because CMake is unavailable in the current environment.
* Independent ECC review ran 10,000 randomized eight-symbol-error patterns through the C ABI; all corrected to the original payload. Review identified shared-library visibility and render-scale bounds, addressed in dev.2.

## Research references and provenance

See [`spec/RESEARCH.md`](spec/RESEARCH.md). FHNW annular codes, CCTag, RuneTag, AprilTag, App Clip Code reverse engineering, HCC2D, PiCode, and IRIS inform comparison only. No external source code is incorporated.

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
