# CKey Format v0.20.0 — Profile 3 Filled Polar Field

Profile 3 retains the 116-unit field, ten byte-interleaved RS(255,k) blocks,
seven capacity/ECC tiers, deterministic visual fill, 232×232 Cartesian pitch,
and 576 calibrated Gray-length/stroke-weight rays.

Its ring matrix now uses 34 concentric rings at radii `3.50 + 0.62r`, for
`r = 0…33`. The innermost usable ring is close to the centre and the outermost
is `23.96`, leaving a measurable gap before the Ring Key at radius `24.45` and
the Ray Key at radius `26.30`. Dots remain radius `0.075`; capacity comes from
filling unused radial space, not shrinking dots.

Cartesian payload sites reserve a 5-unit border moat. This prevents data from
spilling into the 2.6-unit rounded frame under anti-aliasing, display blur, or
camera blur. The Grid Key remains four ordinary `.34` marks on `.5` pitch.

Encoders emit frame format byte `0x14` (decimal 20), profile byte `0x03`.
This changes the codeword-to-site mapping and requires the v0.20 sampling map.
