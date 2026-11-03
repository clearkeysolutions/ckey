# CKey locator format v0.12.0 (draft)

Status: experimental reference format. Encoder output uses `format=12` (`0C`)
and `profile=1`. Formats 8 and 11 remain accepted by the reference decoder
for historical fixtures, but are not emitted.

## Canonical geometry

The field is 29 by 29 units, origin upper-left, with centre `(14.5,14.5)`.
The outer structure is a rounded square from `(0.5,0.5)` to `(28.5,28.5)`,
radius 3.5, width 0.65. It is monochrome; colour is decorative only.

Each finder is **one broad rounded-corner arc integrated with the structural
corner**, rather than multiple nested arcs or an inward-protruding leaf. In
local top-left coordinates the arc centre is `(3.20,3.20)`, its centreline
radius is `2.30`, and width is `0.80`, with round caps. Top-right,
bottom-right, and bottom-left are exact reflections and have continuous arcs.
The top-left arc is split at the 45-degree diagonal. Cap compensation produces
a `0.95`-unit clear visible gap. That gap is the finder orientation signature.
All four 6 by 6 corner envelopes are non-payload.

The polar geometry is unchanged from v0.8.0: four rings at radii
`3.6 + .8r` with 72 five-degree slots, a separate dial band at radii 7.6--9.5,
and a logo-safe centre inside radius 3.3. The Grid Key is the 2 by 2 block at
rows 11--12/columns 2--3. The Ray Key is 12:00/full, 9:00/half, and 9:05/
three-quarter. The Ring Key is dots at slots 18,19,21, an explicit empty slot
20, then a connected round-capped four-slot arc at 22--25. Dot and arc width
are 0.30; the arc visible length equals four dot slots exactly.

## Frame, ECC, and placement

The locator carries exactly 16 bytes. The systematic frame is
`C7 4B 0C 01 00 10`, payload, then CRC-32C of the first 22 bytes. It is
encoded as shortened systematic RS(77,26) over GF(256), primitive polynomial
`0x11D`, primitive element 2, roots `alpha^0...alpha^50`, high-degree first,
with 51 parity bytes. The algebraic bound is 25 unknown byte errors.

The 616 bits are mapped MSB-first by the established alternating
Cartesian/polar placement: angularly interleaved Cartesian cells, all 69 data
dial rays, then round-robin inner ring sites. Structural border, finder
envelopes, Grid/Ray/Ring Keys, and the polar aperture are reserved. The reader
rectifies a supplied quadrilateral and uses permanent keys plus the rounded
finder signature as geometric evidence; it accepts payloads only after RS and
CRC validation. Finder shape is never a substitute for that mathematical
validation.

The normative byte vector is [`locator-v12.json`](../tests/vectors/locator-v12.json).
