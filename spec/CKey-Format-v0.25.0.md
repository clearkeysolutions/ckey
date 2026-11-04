# CKey Format v0.25.0 — Profile 2 Safe Area, Uniform Placement, and Whitening

Status: draft. Supersedes Format 0.23.0 for Profile 2 encoders.

Encoders emit frame format byte `0x19` (decimal 25), profile byte `0x02`.
The byte frame, RS(255,210) code, CRC-32C, and SplitMix32 visual fill of the
210 data bytes are unchanged from Format 0.23.0. The byte-codeword decoder
continues to accept legacy Profile 2 format bytes 9, 15, 18, 19, 22, and 23.
Only the optical layout changed. The image decoder reads this layout only.

## Why this version exists

Format 0.23.0 had three defects, each measured on rendered output:

1. **Dead space.** Cartesian sites were filled row by row, and only 1,020 of 1,428 carried bits, so the band below y = 22.75 was always empty.
2. **Safe-area failure.** The finder moat reserved only a quarter-disc in each finder's outer quadrant. It ignored the arcs' round caps and concave side, so payload modules overlapped finder ink by up to 0.32 units.
3. **Low-entropy payloads printed solid or empty regions.** There was no optical whitening. Ring runs over half a turn were also drawn as the complementary short arc, because the SVG large-arc flag was never set.

## Geometry constants (canonical units, symbol field `[0,29]²`)

| Structure | Definition |
|---|---|
| Frame | rounded square centered at (14.5, 14.5), half-side 14, corner radius 3.5, stroke 0.65 |
| Finders | quarter-circle centerlines of radius 2.3 about (4,4), (25,4), (4,25), (25,25), each in the symbol's outer corner quadrant; stroke 0.8 with round caps. The top-left arc's orientation split does not reduce its clearance |
| Grid Key | rectangle [2.13, 3.87] × [11.13, 12.87] |
| Radial alphabet | data dial rays end at most at r = 8.85 including caps. Ray Key strokes run from r = 7.6 to 7.6 + 1.9·f, with f = 1, 0.5, 0.75 at slots 0, 108 and 110 of 144, half-width 0.11 |

## Safe area

A Cartesian candidate is centered at ((col + ½)/2, (row + ½)/2) for row and col from 0 to 57. Its ink is bounded by the module's circumscribed disc, e = 0.17·√2.

The candidate is a site only if its slack is strictly positive. The slack is the minimum of the following terms:

* frame: `−sd_frame − 0.325 − e − 0.5`
* each finder: `d_centerline − 0.4 − e − 0.5`
* Grid Key: `d_rect − e − 0.5`
* radial alphabet: `r − 8.85 − e − 0.5`
* each Ray Key: `d_segment − 0.11 − e − 0.5`

Every clearance is therefore 0.5 units, one module pitch, edge to edge. These tests use only `+`, `×`, `√`, and comparisons, plus the sine and cosine of the single Ray Key direction at slot 110.

No candidate lies within 1.8 × 10⁻² units of a boundary, so every conforming implementation derives the same site set.

Measured on renders, the minimum ink gaps are: frame 0.775, finders 0.654, Grid Key 0.725.

## Sites

The layout has **2,630** sites:

* 1,377 Cartesian sites.
* 141 dial sites, one per non-key slot of 144.
* Ring sites on rings k = 0…7 at radius 3.4 + 0.43k, 144 slots each. Slots 0, 108, and 110 are excluded on every ring, and slots 36–51 are excluded on ring 7, which carries the Ring Key.

## Placement

Each site has an integer identity `id = kind << 24 | a << 12 | b`:

| Site | kind | a | b |
|---|---|---|---|
| Cartesian | 0 | row | col |
| Dial | 1 | 0 | slot |
| Ring | 2 | ring | slot |

Sites are sorted ascending by `lowbias32(id XOR 0x434B5032)`, where:

```text
lowbias32(x): x ^= x >> 16; x *= 0x7FEB352D; x ^= x >> 15; x *= 0x846CA68B; x ^= x >> 16
```

The function is a bijection, so the order has no ties.

Site i < 2,040 carries codeword bit i, most-significant bit first. Sites i ≥ 2,040 are fill and carry 0.

## Whitening (modulation, not error correction)

The printed value of site i is its codeword or fill bit XOR W(i). W is the most-significant-bit-first bit stream of bytes from SplitMix32, as in Format 0.17's deterministic visual fill: for each byte,
`state += 0x9E3779B9; z = state; z = (z ^ (z >> 16)) * 0x85EBCA6B; z = (z ^ (z >> 13)) * 0xC2B2AE35; z ^= z >> 16;`
and the byte is the low 8 bits of `z` (all arithmetic mod 2³²). The stream starts from state `0x434B5732`; site i takes bit `7 − (i mod 8)` of byte `⌊i/8⌋`.

Whitening only prevents payload-dependent empty or solid regions. It provides no secrecy and no error correction.

A printed value of 1 means:

* Cartesian: a module is drawn.
* Dial: a long ray (length 1.20) instead of a short ray (length 0.52).
* Ring: a dot is drawn. Adjacent set slots on a ring join into one round-capped arc. An arc spanning more than π **must** set the SVG large-arc flag.

## Rendering (normative geometry)

The canvas is viewBox `−1 −1 31 31`. The background is filled `#050505` and all ink is `#F4F1E8`. Inverted polarity is equally valid.

| Element | Geometry |
|---|---|
| Frame | rect (0.5, 0.5, 28 × 28), rx 3.5, stroke 0.65 |
| Finders | per corner, an arc of radius 2.3 about the inset corner (4 from each edge), stroke 0.8, round caps. The top-left arc is split symmetrically about 225° so the clear gap between cap edges is 0.95 |
| Grid Key | rects at (c + 0.13, r + 0.13), 0.74 × 0.74, rx 0.16, for r ∈ {11, 12} and c ∈ {2, 3} |
| Ray Key | strokes from r = 7.6 to 7.6 + 1.9·f at slots 0, 108, 110 of 144 (f = 1, 0.5, 0.75), width 0.22, round caps |
| Ring Key | dots of radius 0.10 at r = 6.41, slots 36, 38, 42; plus one run from slot position 43.5 to 51.5 |
| Cartesian site | rect centered on the site, 0.34 × 0.34, rx 0.08 |
| Dial site | stroke from r = 7.6 outward, length 1.20 (printed 1) or 0.52 (printed 0), width 0.10, round caps |
| Ring site | dot of radius 0.10 at r = 3.4 + 0.43k |

**Ring runs.** A maximal run of printed slots s…e−1 on one ring is drawn as a single run. A run of one slot is a dot. A longer run is an arc on that ring's radius from slot position s − 0.5 to e − 0.5, shortened at each end by the angle `dot_radius / radius`. It has stroke width 2 × dot radius, round caps, clockwise sweep, and the large-arc flag set when its span exceeds π.

Angles are measured clockwise from 12:00: a point at radius r and slot k of N is (14.5 + r·sin(2πk/N), 14.5 − r·cos(2πk/N)).

SVG group classes (`ckey-frame`, `ckey-finders`, `ckey-grid-key`, `ckey-polar-keys`, `ckey-data-polar`, `ckey-data-cartesian`) are informative.

## Conformance vector

For the 18-byte payload `"CKey renderer test"`, compute FNV-1a-64 over every site in placement order: the site's 32-bit identity (little-endian), then its printed value as one byte. The result is `17A92D978EE7B3CF`. The first three site identities are `00026034`, `0000900B`, `02001036`.

## Decoding (informative)

The reference decoder samples each feature class with a support matched to its width: 0.06 for Cartesian modules, 0.025 for dots, and 0.02 for rays.

It normalizes samples between the Grid Key and two references that are guaranteed empty, (1.5, 11.5) and (5.5, 14.5). It then thresholds each class from its own samples using two-means. Whitening guarantees each class is roughly half printed.

Ring sites are re-thresholded by neighbour context, since a blurred isolated dot peaks lower than a dot inside an arc. Fill sites' printed values are known and anchor that context.

Orientations whose fixed keys disagree, with less than 80% of key samples matching, are rejected before full sampling.

Measured with an exact quadrilateral, decoding needs at least 10 px per unit for a clean render or one 3×3 blur, and 12 px per unit for two blurs.
