# CKey Format v0.26.0 — Profile 3 Safe Area, Uniform Placement, Whitening, and Polar Join

Status: draft. Supersedes Format 0.24.0 for Profile 3 encoders.

Encoders emit frame format byte `0x1A` (decimal 26), profile byte `0x03`.
The byte frame, ten byte-interleaved RS(255,k) blocks, seven capacity tiers, CRC-32C, and SplitMix32 visual fill are unchanged from Format 0.24.0. The byte-codeword decoder continues to accept tiered format bytes 17, 20, 21, and 24, and legacy bytes 10 and 16. Only the optical layout changed. The image decoder reads this layout only.

## Why this version exists

Format 0.24.0 had four defects, each measured on rendered output:

1. **Dead ray band.** Ray components were placed after every ring site, so only 173 of 573 rays carried data. The remaining 400 rendered as identical blank stubs.
2. **Patchy Cartesian field.** Only 10,200 of 31,312 Cartesian sites carried data, leaving sparse areas.
3. **Safe-area failure.** The quarter-disc finder moat let modules overlap finder ink by up to 1.5 units.
4. **Dark annulus.** A 1.85-unit gap separated the Ring Key from the ray alphabet.

Format 0.24.0 also had no whitening, and long ring runs were drawn as the wrong arc.

## Geometry constants (canonical units, symbol field `[0,116]²`)

| Structure | Definition |
|---|---|
| Frame | rounded square centered at (58, 58), half-side 56, corner radius 14, stroke 2.6 |
| Finders | quarter-circle centerlines of radius 9.2 about (16,16), (100,16), (16,100), (100,100) in the outer corner quadrants; stroke 3.2 with round caps |
| Grid Key | four 0.34-unit modules within [8.08, 8.92] × [45.08, 45.92] |
| Rings | 34 rings from r = 7.0 to **24.62** (was 23.96), equal pitch |
| Ring Key | at **r = 25.11** (was 24.45), occupying slots 72–79 of 288. Its centerline gap to the ray alphabet's inner end is now 1.19 units, matching Profile 2; the ink-to-ink gap is about 0.98 |
| Rays | start at r = 26.30. Data rays end at most at r = 29.79 including caps. Ray Key strokes are 3.6·f long, with f = 1, 0.5, 0.75 at slots 0, 432 and 440 of 576, half-width 0.14 |

## Safe area

A Cartesian candidate is centered at ((col + ½)/2, (row + ½)/2) for row and col from 0 to 231, with e = 0.17·√2. It is a site only if every slack term is positive. The terms are the same as Profile 2's, with these clearances:

| Structure | Clearance |
|---|---|
| Frame | 1.5 |
| Finders | 1.5 |
| Grid Key | 0.5 |
| Radial alphabet | 0.5 |
| Ray Keys | 0.5 |

The frame and finder clearances scale with the structures, which are four times Profile 2's. This keeps them separable from payload when a distant capture blurs the small modules together.

The closest candidate lies 3 × 10⁻⁴ units from a boundary.

Measured on renders, the minimum ink gaps are: frame 1.812, finders 1.601, Grid Key 0.688.

## Sites

The layout has **42,645** sites:

* 31,244 Cartesian sites.
* 9,682 ring sites. Ring slots 0, 216, and 224 are excluded on every ring. Slots 72–79 are also excluded on ring 33, as a deliberate clear field radially inside the Ring Key, so the key is read without adjacent payload.
* 1,719 ray component sites: three per non-key ray. Components 0 and 1 are the Gray-coded length bits and component 2 is the width bit.

## Placement and whitening

Each site has an integer identity `id = kind << 24 | a << 12 | b`:

| Site | kind | a | b |
|---|---|---|---|
| Cartesian | 0 | row | col |
| Ring | 2 | ring | slot |
| Ray component | 3 | component | slot |

Sites are sorted ascending by `lowbias32(id XOR 0x434B5033)`, using the same `lowbias32` as Format 0.25.

Site i < 20,400 carries codeword bit i, most-significant bit first. Later sites are fill and carry 0. Each site kind carries 47.7–48.2% codeword bits, so data is spread over the whole symbol.

The printed value is the bit XOR W(i). W is the most-significant-bit-first bit stream of bytes from SplitMix32, as in Format 0.17's deterministic visual fill: for each byte,
`state += 0x9E3779B9; z = state; z = (z ^ (z >> 16)) * 0x85EBCA6B; z = (z ^ (z >> 13)) * 0xC2B2AE35; z ^= z >> 16;`
and the byte is the low 8 bits of `z` (all arithmetic mod 2³²). The stream starts from state `0x434B5733`. Whitening is modulation, not error correction.

A ray's printed values are its three components' printed values, g₁ (component 0) and g₀ (component 1), plus w (component 2). The 2-bit Gray code is `g = 2·g₁ + g₀`, and the length level is `level = g XOR (g >> 1)`, giving g = 0, 1, 3, 2 → level 0, 1, 2, 3. The printed length is 0.80 + 0.85·level, and the width is 0.28 if w = 1, otherwise 0.16.

Ring runs over half a turn **must** set the SVG large-arc flag.

## Rendering (normative geometry)

The canvas is viewBox `−4 −4 124 124`. The background is filled `#050505` and all ink is `#F4F1E8`. Polar coordinates are about (58, 58), clockwise from 12:00, as in Format 0.25.

| Element | Geometry |
|---|---|
| Frame | rect (2, 2, 112 × 112), rx 14, stroke 2.6 |
| Finders | per corner, an arc of radius 9.2 about the inset corner (16 from each edge), stroke 3.2, round caps. The top-left arc is split symmetrically about 225° with a 3.8 clear gap between cap edges |
| Grid Key | rects at (8.08 + 0.5c, 45.08 + 0.5r), 0.34 × 0.34, rx 0.08, for r, c ∈ {0, 1} |
| Ray Key | strokes from r = 26.30 to 26.30 + 3.6·f at slots 0, 432, 440 of 576 (f = 1, 0.5, 0.75), width 0.28, round caps |
| Ring Key | dots of radius 0.075 at r = 25.11, slots 72, 73, 75; plus one run from slot position 75.5 to 79.5 |
| Cartesian site | rect centered on the site, 0.34 × 0.34, rx 0.08 |
| Ray | stroke from r = 26.30 outward with the printed length and width, round caps |
| Ring site | dot of radius 0.075 at r = 7.0 + k·(24.62 − 7.0)/33 |

Ring runs are drawn exactly as in Format 0.25: arcs from s − 0.5 to e − 0.5, shortened by `0.075 / radius` at each end, stroke width 0.15, with the large-arc flag set when the span exceeds π.

## Conformance vector

For the payload `"CKey renderer test"`, the FNV-1a-64 over (identity little-endian, printed value) for every site in placement order is `762D03B44DB9C502`.

## Decoding (informative)

The reference decoder reads each class with a feature-sized support. Ray width is read as integrated ink coverage across the ray, which blur conserves.

Each class is thresholded from its own normalized samples using two-means. Ring sites are then re-thresholded by neighbour context (0, 1, or 2 printed ring neighbours), anchored by the known values of fill sites. Without this step, a blurred isolated dot falls below the arc-dominated class threshold. Orientations with less than 80% key agreement are rejected before full sampling. The references are the Grid Key and two points that are guaranteed empty: (7.75, 45.5) inside the Grid Key clearance, and (58, 4.5) inside the frame moat. The calibration score's polar empty reference moved from r = 28.0, a data ray position, to r = 25.7 in the Ring Key/ray gap.

Measured with an exact quadrilateral, decoding the 2,048-byte tier needs at least 11 px per unit for a clean render, and 10–16 px per unit decodes after one or two 3×3 blurs. Wide rays sit only 0.007 units apart at the alphabet's inner end, so ray width is the profile's tightest optical margin.
