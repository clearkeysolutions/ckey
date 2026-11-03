# CKey Format v0.15.0 — Profile 2 Text Capacity

Status: experimental high-capacity profile. This revision emits `format=15`
(`0F`), `profile=2`. It supersedes Format 0.9.0 for new Profile 2 symbols;
decoders retain Format 0.9.0 readability.

## Frame and correction

The frame before RS encoding is `C7 4B 0F 02 LLhi LLlo payload CRC32C`, then
deterministic zero padding through byte 209. Profile 2 carries 1–200 UTF-8 or
binary bytes in systematic RS(255,210) over GF(256), primitive polynomial
`0x11D`, primitive element 2, roots `alpha^0...alpha^44`. The 45 parity bytes
give the algebraic bound `2e + s <= 45`, including 22 unknown-byte errors.

## Canonical finder geometry

The normalized field remains 29 by 29 units with centre `(14.5,14.5)`. The
outer boundary is a continuous 0.65-unit rounded square from `(0.5,0.5)` to
`(28.5,28.5)`, radius 3.5. Each corner has exactly **one** broad, round-capped
arc inside the border. In the top-left local corner its centre is `(4,4)`,
radius `2.30`, stroke width `0.80`, and its exterior-quadrant path runs from
`(1.70,4.0)` to `(4.0,1.70)`. Its concave/open mouth faces down-right into the
polar matrix. The other corners are reflected copies. Only the top-left arc is
split at 225 degrees, with a cap-compensated 0.95-unit visible gap. It is the
orientation signature. The 6 by 6 physical-unit corner envelopes remain
reserved.

## Density and placement

The Cartesian matrix has a 58 by 58 density grid at 0.5-unit pitch and
0.34-unit rounded-square modules. The central polar aperture (radius 10.5),
finders, and Grid Key are reserved.

There are 144 equal angular slots, eight dot/arc rings at radii
`3.40 + 0.43*r`, and a narrow ray dial. Ring-bit runs of length one are dots;
runs of length at least two are one round-capped connected arc. The visible
length of a four-slot arc equals four adjacent dot slots exactly, while the arc
remains a distinct connected module. Rays use 0.52-unit short and 1.20-unit
long states. The Grid/Ray/Ring Keys retain their calibration roles.

The native core has framing, RS correction, SVG rendering, and codeword tests.
It is not camera-validated; do not advertise Profile 2 as camera-scannable.
