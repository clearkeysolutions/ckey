# CKey Format v0.22.0 — Profile 2 Corrected Finder Clearance

Profile 2 retains its 29-unit field, 58×58 Cartesian pitch, eight ring rows,
144 radial slots, full RS(255,210) protection, and deterministic visual fill.

Cartesian payload clearance at each corner is limited to the actual outside
quadrant of the curved finder and its measured stroke guard. It is not a
square corner reservation. This removes the four dead corner squares while
keeping data separated from both the rounded frame and finder ink.

Encoders emit frame format byte `0x16` (decimal 22), profile byte `0x02`.
This changes the Profile 2 codeword-to-site mapping and requires the v0.22
sampling map.
