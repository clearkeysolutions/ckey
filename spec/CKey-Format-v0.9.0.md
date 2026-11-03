# CKey Format v0.9.0 — Profile 2 Text Capacity

Status: experimental high-capacity profile. This document adds `format=9`, `profile=2`; it does not change or invalidate the camera-validated Format 0.8.0 locator profile.

## Purpose and capacity

Profile 2 carries a payload of 1–200 UTF-8 or binary bytes. It uses a full-length systematic RS(255,210) code over GF(256): 210 data bytes plus 45 parity bytes. The guaranteed algebraic bound is `2e + s <= 45`, including up to 22 unknown byte errors. A 6-byte header and CRC-32C consume 10 of the 210 data bytes, leaving 200 payload bytes. This is the first large-payload tier, not the aspirational 2,048-byte tier; 2,048 bytes requires block interleaving and a future format revision.

Frame bytes, before RS encoding: `C7 4B 09 02 LLhi LLlo payload CRC32C`. The CRC covers the header through the payload. The remaining data region through byte 209 is zero padding. The 45 parity bytes are appended systematic RS parity, with primitive polynomial `0x11D`, primitive element 2, and roots `alpha^0...alpha^44`.

## Shared geometry

The normalized field remains 29 by 29 units with center `(14.5,14.5)`, the same 0.65-unit rounded boundary, the same three thick continuous corner arcs, and the same one-module visible split in the top-left finder. The 6 by 6 physical-unit corner envelopes remain reserved. Thus the finder, grid, ray, and ring proportions remain visually recognizable across profiles.

The permanent keys retain their semantic roles:

* **Grid Key:** the fixed 2 by 2 physical-unit left-side block at rows 11–12 / columns 2–3.
* **Ray Key:** 12:00 at 100%, 9:00 at 50%, and its adjacent 9:05 bearing at 75%. At 144 slots these occupy slots 0, 108, and 110.
* **Ring Key:** on outer ring 7 at slots 36, 38, and 42, one reserved-empty slot 40, plus an eight-slot continuous arc at slots 44–51. The double slot count preserves the original 5-degree key proportions.

## Density and data placement

The Cartesian matrix has a 58 by 58 density grid (`0.5` physical unit pitch); its rounded-square payload modules are `0.34` physical units wide. Physical finder envelopes, the Grid Key, and the central polar aperture (radius 10.5) remain reserved. This is density scaling, not finder shrinking.

The polar matrix has 144 equal angular slots (2.5 degrees), eight dot/arc rings at radii `3.40 + 0.43*r` for `r=0...7`, and a narrow dial band outside the rings. The dial uses a 0.52-unit short ray for `0` and a 1.20-unit long ray for `1`; every available data slot is present, so absence is never a dial data state.

Polar ring bits are occupancy bits. A one-bit run of length one renders as one dot. A maximal consecutive one-bit run of length at least two renders as **one round-capped continuous arc** covering exactly those angular slots. The arc’s centerline is shortened by one terminal radius at each end, so its visible curved length equals the sum of its occupied slot pitches. Therefore a four-slot arc has exactly the visible angular span of four adjacent dots, but is topologically a connected mark and is decoded as the corresponding run. This makes multi-unit arcs payload-bearing geometry rather than decoration.

The 2,040 codeword bits are placed deterministically by alternating Cartesian and polar candidates. The implementation must expose the candidate ordering as a test vector before independent camera interoperability is claimed.

## Decoder and interoperability status

The native core implements profile-2 framing, RS encode/decode, CRC validation, deterministic SVG rendering, and ring-run arcs. Its grayscale camera decoder currently supports only the Format 0.8.0 locator. Profile 2 must not be advertised as camera-scannable until the same key-driven projective sampling and a held-out physical-capture corpus pass for this denser geometry.

