# CKey Format v0.16.0 — Profile 3 Extended Capacity

Status: experimental byte-codeword and renderer profile. This revision emits
`format=16` (`10`), `profile=3`. It supersedes Format 0.10.0 for new Profile 3
symbols; decoders retain Format 0.10.0 readability. It is **not camera
validated**.

## Frame and correction

Profile 3 carries 1–2,048 binary bytes. Its frame is
`C7 4B 10 03 LLhi LLlo payload CRC32C`, zero-padded to 2,230 data bytes. The
CRC-32C covers all preceding frame bytes. Data are divided into ten 223-byte
blocks, each systematic RS(255,223) over GF(256), primitive polynomial `0x11D`,
primitive element 2, roots `alpha^0...alpha^31`. Each block has 32 parity bytes
and corrects up to 16 unknown byte errors. Output byte `10*i+b` is byte `i`
from block `b`, distributing local loss across blocks.

## Canonical finder geometry

The field is 116 by 116 units. Its outer boundary is the continuous rounded
square from `(2,2)` to `(114,114)`, radius 14, width 2.6. Each finder is one
broad, round-capped arc inside that border. The top-left local finder is centred
at `(16,16)`, radius `9.20`, width `3.20`, with its exterior-quadrant path from
`(6.80,16)` to `(16,6.80)`. The concave/open mouth faces down-right, toward the
polar field. Reflection creates the other three corners; only top-left is split
at its diagonal with a cap-compensated 3.8-unit visible gap. Finder reservations
cover only their strokes plus guard band, never an arbitrary square outside a
corner.

## Capacity geometry

The Cartesian matrix is 232 by 232 at 0.5-unit pitch, with 0.34-unit modules.
The polar matrix has 24 rings, 288 slots per ring, radii `9.50 + 0.65*r`, and
uniform dot radius 0.075. A run of two or more occupied slots is a connected,
round-capped arc; its angular span is exactly its occupied slot count, so a
four-slot arc equals four dots' slot span and stroke width.

The ray matrix has 576 bearings beginning at radius 26.30. Aside from three
reserved Ray Key bearings, every ray encodes three bits using four Gray-coded
lengths (0.80, 1.65, 2.50, 3.35) and calibrated 0.16/0.28-unit weights. The
Grid Key uses ordinary payload geometry to establish pitch and clear space;
fixed-size or fixed-distance decoder ladders are non-conformant.

The core validates Profile 3 codewords and renders its exact geometry, but the
mobile/image decoder does not sample this profile. It must not be advertised as
scannable until projective sampling and physical-capture testing pass.
