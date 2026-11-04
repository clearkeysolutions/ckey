# CKey Format v0.24.0 — Profile 3 Finder Moat and Polar Join

Profile 3 reserves a `13.25`-unit radial moat only in each finder’s actual
outer corner quadrant. This prevents modules from visually touching finder
ink without creating inward wedges. Cartesian payload begins outside radius
`30.6`; the polar field therefore has only a measured 0.9-unit gap after its
longest ray.

Encoders emit format byte `0x18` (decimal 24), profile byte `0x03`.
