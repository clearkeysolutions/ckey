# Changelog

## 0.1.0-dev.2 — 2026-11-02

* Corrected the format note to reflect the implemented byte-codeword RS decoder and its tested eight-error correction bound.
* Fixed shared-library C ABI exports and bounded SVG render scale to prevent dimension overflow and unreasonable outputs.
* Added independent-review findings and synchronized the public software version and fixture metadata.

## 0.1.0-dev.1 — 2026-11-02

* Added draft locator-v1 framing with CRC-32C and shortened RS(42,26) over GF(256).
* Added C++20 encoder and byte-codeword decoder with correction of up to eight unknown byte errors.
* Added fixed Cartesian/polar calibration geometry and monochrome SVG renderer.
* Added C ABI, CLI encoder, independent RS parity reference/vector, unit tests, and CI workflow.
* Recorded versioning, context, research, license, and milestone rules.
* Not included: erasure correction, image/camera decoder, perspective recovery, SDKs, browser lab, or measured device benchmarks.
