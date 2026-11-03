# CKey living project context

**Current software version:** `0.1.0-dev.3`
**Current wire format:** `CKey Format 0.2.0` (format byte `02`, locator profile byte `01`)
**Branch:** `main`
**Context updated:** 2026-11-02

## Mission

CKey is an independent, monochrome optical encoding standard that must work without Viadere or any other service. Its defining geometry combines a rounded-square Cartesian field and corner finders with a polar interior containing radial rays, discrete dots, concentric rings, and connected arcs. The project must deliver mathematical correctness and camera-tested interoperability before capacity or speed claims.

## Decisions that currently govern implementation

* The repository's inherited MIT license is retained for now. Do not copy research implementations without auditing exact source and dependency licenses.
* Software versioning uses Semantic Versioning with prerelease identifiers during development. `VERSION`, the public API version string, release metadata, and this context must agree.
* The locator profile carries exactly 16 bytes. Its frame is magic/version/profile/length/payload/CRC-32C; the 26-byte frame is encoded with shortened systematic RS(42,26) over GF(256). ECC is a mathematical error-correcting code, not masking, XOR obscuring, or cosmetic transformation.
* RS parameters are primitive polynomial `0x11D`, primitive element 2, roots `α^0…α^15`, high-degree coefficient first, with parity appended. CRC-32C is integrity detection, separate from error correction.
* Geometry is fixed at 29 × 29 for profile 1. The Grid, Ray, and Ring Keys are structural and reserved. The data sites alternate Cartesian and polar coordinate families; exact definitions live in the matching versioned format document. Format 0.2.0 replaces blocky QR-like corner finders with open nested corner arcs and specifies the monochrome reference renderer in the dark-field/light-mark polarity used by the supplied targets.
* No claim that camera decoding, perspective recovery, sub-200 ms latency, or the aspirational 2,048-byte profile is complete or achieved. Current code encodes/renders SVG and the C ABI decodes byte codewords with up to 8 unknown byte-error corrections. Tests cover framing, CRC, an independent RS parity vector, 32 full-bound correction patterns, and deterministic SVG/encoding. Erasure decoding is not implemented.

## Current repository state

Milestone 1 is in progress: C++20 core, C ABI, locator encoder and byte-codeword decoder, deterministic geometry placement, target-aligned monochrome SVG renderer, CLI, versioned draft formats, research/license audit, independent RS reference, and unit tests. The previous 0.1 renderer was visually rejected; 0.2 replaces its QR-like block finders with nested open corner arcs, rounded modules, and dark-field/light-mark presentation. This is a geometry/rendering revision, not a claim that the supplied 13-state image is decoded by this binary profile. Direct C++20 compiler build and tests passed in this environment; CMake is not installed, so CMake/CTest was not run. There are no third-party runtime dependencies in the core.

Next engineering gates: (1) implement RS erasure correction and expose decode in a CLI; (2) implement image localization, normalization, and perspective correction with held-out real camera captures; (3) add native and WASM/JS SDKs backed by the shared core; (4) build the browser lab and release/benchmark automation. Update this context in the same change that moves any gate or changes a governing decision.

## Validation recorded for this version

* `g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic`: core compiled without warnings.
* C++ core tests passed, including 32 distinct eight-byte-error correction patterns.
* `python3 tools/check_version.py`: software version nodes synchronized.
* `python3 tools/rs_reference.py`: independently generated RS parity matched the committed vector.
* CLI generated the example SVG; Python XML parser accepted it.
* Static and hidden-visibility shared builds passed; shared build exported all five public `ckey_*` ABI functions. Render-scale boundary checks passed.
* CMake/CTest not run because CMake is unavailable in the current environment.
* Independent ECC review ran 10,000 randomized eight-symbol-error patterns through the C ABI; all corrected to the original payload. Review identified shared-library visibility and render-scale bounds, addressed in dev.2.

## Research references and provenance

See [`spec/RESEARCH.md`](spec/RESEARCH.md). FHNW annular codes, CCTag, RuneTag, AprilTag, App Clip Code reverse engineering, HCC2D, PiCode, and IRIS inform comparison only. No external source code is incorporated.

## How to use this context

Before editing, read this document, `VERSION`, and the format document matching the current format version. Check the current repository HEAD and use the latest committed state; do not base edits on stale local copies, archived specs, or unverified prior conversation snippets. After changes, update the version and this context together and report the exact version/commit. The newest committed version is authoritative.
