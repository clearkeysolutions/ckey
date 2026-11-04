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
| Ring Key | at **r = 25.11** (was 24.45), slots 72–79 of 288. Its gap to the ray alphabet is now 1.19 units, matching Profile 2 |
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
* 9,682 ring sites. Ring slots 0, 216, and 224 are excluded on every ring, and slots 72–79 are excluded on ring 33.
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

The printed value is the bit XOR W(i), where W comes from SplitMix32 starting at state `0x434B5733`. Whitening is modulation, not error correction.

A ray's printed length level is Gray-decoded from components 0 and 1, giving length 0.80 + 0.85·level. Its printed width is 0.28 when component 2 is 1, otherwise 0.16.

Ring runs over half a turn **must** set the SVG large-arc flag.

## Decoding (informative)

The reference decoder reads each class with a feature-sized support. Ray width is read as integrated ink coverage across the ray, which blur conserves.

Each class is thresholded from its own normalized samples using two-means. The references are the Grid Key and two points that are guaranteed empty: (7.75, 45.5) inside the Grid Key clearance, and (58, 4.5) inside the frame moat. The calibration score's polar empty reference moved from r = 28.0, a data ray position, to r = 25.7 in the Ring Key/ray gap.

Measured on clean renders with an exact quadrilateral, decoding the 2,048-byte tier needs at least 11 px per unit. Wide rays sit only 0.007 units apart at the alphabet's inner end, so ray width is the profile's tightest optical margin.
