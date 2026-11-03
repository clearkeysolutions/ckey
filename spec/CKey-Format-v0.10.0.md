# CKey Format v0.10.0 — Profile 3 Extended Capacity

Status: experimental byte-codeword and renderer profile. This profile has deterministic C++ encode, decode, SVG rendering, and correction tests. It is **not camera validated**; no client may advertise it as scannable until its projective sampler and physical-capture corpus pass.

## Frame and correction

Profile 3 carries 1–2,048 binary bytes. Its frame is `C7 4B 0A 03 LLhi LLlo payload CRC32C`, zero-padded to 2,230 data bytes. The CRC-32C covers the bytes before it.

The data are divided into ten 223-byte blocks. Each is systematic `RS(255,223)` over GF(256), primitive polynomial `0x11D`, primitive element 2, roots `alpha^0…alpha^31`. Every block has 32 parity bytes and corrects up to 16 unknown byte errors. The ten 255-byte codewords are byte-interleaved for output: output byte `10*i+b` is byte `i` from block `b`. Total encoded capacity is 2,550 bytes / 20,400 bits; total parity is 320 bytes.

## Profile 3 geometry

The field is 116 by 116 units. The rounded boundary, corner finder envelopes, central aperture, keys, and all payload coordinates scale with the field; they are not made smaller merely to claim capacity. The Cartesian matrix is 232 by 232 at a 0.5-unit pitch. Its data modules remain 0.34 unit wide.

The polar interior is deliberately increased through distinct calibrated states:

* **Ring matrix:** 24 concentric rings, 288 equal angular slots per ring, radii `9.50 + 0.65*r`. A dot has radius 0.075. The radial gap is more than four dot diameters. Set runs of two or more slots render as a connected, round-capped arc whose angular span is exactly its occupied slot span. A four-slot arc occupies the same angular extent and stroke width as four adjacent dots.
* **Ray matrix:** 576 bearings beginning at radius 26.30. Three fixed Ray Key bearings are reserved: 12:00/full, 9:00/half, and 9:05/three-quarter. Every other ray is always rendered and carries three bits: two Gray-coded length bits select 0.80, 1.65, 2.50, or 3.35 units; one bit selects a 0.16 or 0.28 unit stroke. This is an eight-state calibrated alphabet, not an absence/presence shortcut.
* **Grid Key:** a fixed 4 by 4 array of known 1.45-unit marks with 2.1-unit pitch at the left side establishes local pitch and clear-space evidence before dense Cartesian sampling.
* **Ring Key:** outer ring slots 72/73 are dots, 74 is reserved empty, 75 is a dot, and 76–79 are one connected four-slot arc.

The placement sequence alternates Cartesian candidates with polar candidates. Cartesian candidates use a deterministic two-dimensional permutation, so a finite codeword populates the full usable annulus rather than leaving a row-major or radial-fill dead zone. The profile-specific image decoder must derive module scale, angular pitch, ray length classes, and ray weight classes from the Grid/Ray/Ring Keys under its homography. Fixed image-size or fixed-distance ladders are non-conformant.

## Physical requirement

This is a large-format profile. The encoder must emit a sufficiently large raster or print so the smallest 0.34-unit Cartesian module and the 0.16-unit thin ray meet the selected camera's documented pixel-per-feature floor. Increasing an SVG's dimensions does not itself prove a camera can decode it.
