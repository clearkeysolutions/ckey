## 0.1.0-dev.9 — 2026-11-03

* Restored three nested quarter-arc finders at all corners, with the three top-left arcs split by a one-module orientation gap.
* Made all inner-ring and Ring Key dots the same 0.30-module diameter; Ring Key arc caps are round while its visible length remains exactly four angular pitches.
* Encoded each of the 69 data dial bits by a short 0.65-module ray or long 1.35-module ray; calibration rays remain reserved and fixed.
* Versioned the wire geometry as CKey Format 0.7.0 / format byte 07 and regenerated the CRC-32C/Reed–Solomon example vector. The RS code and C ABI are unchanged.

## 0.1.0-dev.8 — 2026-11-03

* Increased the central clear radius and moved the four rings outward into a tighter radial group.
* Sized each ring dot to 72% of its adjacent-slot chord, so marks are visibly sized and closely spaced at every ring radius.
* Set the Ring Key arc width to one dot diameter and its centerline length to exactly four slot intervals.
* Versioned the geometry as CKey Format 0.6.0 / format byte 06; the RS(77,26) code and ABI remain unchanged.

## 0.1.0-dev.7 — 2026-11-03

* Fixed oversized ring dots: SVG circles inherited the parent group’s default 1-module stroke. Circle marks now explicitly use no stroke, rendering at their specified 0.12-module diameter.
* Added a renderer regression assertion for stroke-free 0.06-radius dots. The four-slot Ring Key arc remains 0.12 modules wide and spans the same four angular slots.

## 0.1.0-dev.6 — 2026-11-03

* Reduced ring data dots and Ring Key dots to 0.12-unit diameter; made the four-slot Ring Key arc exactly match four angular slots and dot diameter.
* Increased locator protection to shortened RS(77,26), adding 51 mathematically computed parity bytes and using 616 of 621 eligible sites for codeword bits.
* Bumped the C ABI because the public codeword grew from 42 to 77 bytes.
* Versioned the wire geometry as CKey Format 0.5.0 / format byte 05 and refreshed the independent vector.

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
* Kept Reed–Solomon error correction and CRC-32C framing; geometry evolved independently of framing.

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
