# CKey Format v0.21.0 — Profile 3 Corrected Field Geometry

Profile 3 retains its 116-unit field, ten byte-interleaved RS(255,k) blocks,
seven capacity/ECC tiers, deterministic visual fill, 232×232 Cartesian pitch,
and 576 calibrated Gray-length/stroke-weight rays.

The 34-ring matrix occupies radii `7.00 + ((23.96 - 7.00) / 33)r`, for
`r = 0…33`. This retains the full ring capacity while restoring a 7-unit
measurable centre quiet zone. The Ring Key remains at radius `24.45` and the
Ray Key begins at radius `26.30`.

Finder clearance is reserved only in each finder's actual outer corner
quadrant. The Cartesian field therefore does not print under finder arcs and
does not create false inward diagonal voids. The 5-unit border moat and the
four `.34` Grid Key modules on the `.5` Cartesian pitch are unchanged.

Encoders emit frame format byte `0x15` (decimal 21), profile byte `0x03`.
This changes the Profile 3 codeword-to-site mapping and requires the v0.21
sampling map.
