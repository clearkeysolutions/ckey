## 0.1.0-dev.5 — 2026-11-03

* Updated locator geometry to one thick arc at three corners and a split top-left orientation arc with a one-module visible gap; enlarged the reserved corner envelopes to eliminate finder/data collisions.
* Replaced the previous vertical calibration sequence with the specified 2×2 Grid Key.
* Spread payload placement across angular sectors of the Cartesian matrix and round-robin across the dial plus all four inner rings.
* Versioned the wire format as 0.4.0 / format byte 04 and regenerated the CRC-32C/Reed–Solomon interoperability vector.
* Added maintained geometry and version context for the supplied blueprint.

# Changelog

## 0.1.0-dev.4 — 2026-11-02

* Reworked the polar field into four inner concentric rings plus a separate 1.9-unit annular dial band; all 12:00/9:00/9:05 calibration rays now stay within that band rather than extending to the center.
* Added a round-robin angular traversal, retained the outer Cartesian matrix, and replaced corner dots with three nested arcs; split the top-left finder into two arc segments for orientation.
* Advanced the wire format to 0.3.0 and preserved earlier version specs and fixtures.

## 0.1.0-dev.3 — 2026-11-02

* Replaced solid QR-like block finders with nested open corner arcs and switched the reference SVG to dark-field/light-mark styling with rounded data modules to follow the supplied target geometry.
* Advanced the wire geometry version to CKey Format 0.2.0; preserved the 0.1.0 draft and fixture as historical versions.
* Kept RS(42,26) and CRC-32C framing mathematical encoding unchanged apart from the updated format byte.

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
