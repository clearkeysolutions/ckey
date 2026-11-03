# CKey locator format v0.4.0 (draft)

Status: experimental implementation profile. Wire and geometry constants are versioned as `format=4, profile=1`; interoperability is not promised until decoder fixtures pass in independent implementations.

## 1. Coordinate system and symbol shape

The canonical symbol field is 29 × 29 module units. The origin is the upper-left; x increases right and y increases down. Its center is `(14.5, 14.5)`. A 0.65-unit rounded square boundary runs from `(0.5,0.5)` to `(28.5,28.5)` with radius 3.5. Each corner finder is one quarter-arc with centerline radius 4.5 and stroke width 1.0 using round caps. Its top-left center is exactly `(0.8,0.8)`; the top-right, bottom-right, and bottom-left centers are the corresponding reflections `(28.2,0.8)`, `(28.2,28.2)`, and `(0.8,28.2)` across the 29-unit field. Bottom-left, top-right, and bottom-right arcs are continuous. The top-left finder is the same arc split into two pieces with an exact two-module centerline arc-length gap centered at 45°. With round caps, this gives a nominal one-module visible gap (approximately 0.997 module by endpoint clearance); each fragment has the same thickness as the other finders. The gap is an orientation cue, not a payload site. Each 6 × 6 corner region (module indices 0–5 or 23–28 on each axis) is reserved from payload. This envelope includes the finder stroke and prevents nearby matrix cells from touching it. Boundary and finder geometry are structural and take precedence over other layers. These dimensions remain fixed for profile 1.

The polar angle `a` is clockwise from 12:00. There are 72 angular slots per turn, each exactly 5 degrees. Four inner concentric data rings have radii 2.8, 4.0, 5.2, and 6.4 units. Polar point `(ring, slot)` has radius `2.8 + 1.2 × ring` for rings 0 through 3, and center

`x = 14.5 + radius × sin(2π × slot / 72)`  
`y = 14.5 − radius × cos(2π × slot / 72)`.

The dial is a separate narrow annulus from radius 7.6 to 9.5 units. Its 72 angular slots contain short radial data ticks; no tick extends toward the center. The symbol is monochrome. The reference renderer uses a dark background and light marks; a decoder must normalize either contrast polarity from image evidence. Color never changes interpretation. Reference SVG colors `#050505` and `#f4f1e8` are decorative only.

## 2. Permanent calibration keys

These are reserved structures and cannot be payload sites.

* **Grid Key:** A fixed 2 × 2 block occupies columns 2–3 and rows 11–12. All four rounded-square cells are dark. Their known spacing calibrates Cartesian pitch and verifies local grid alignment.
* **Ray Key:** Rays occupy only the dial annulus. Slot 0 (12:00) spans radius 7.6–9.5; slot 54 (9:00) spans the inner half, 7.6–8.55; adjacent slot 55 (9:05) spans the inner 75%, 7.6–9.025. None touches the center.
* **Ring Key:** On inner ring 3 (radius 6.4), slots 18 and 19 are individual dots; slot 20 is explicitly empty; slot 21 is one individual dot; slots 22–25 are one connected, continuous four-slot arc. A dot is a circular mark centered in one angular slot. An arc is one connected stroke spanning four slot intervals, with no internal gaps or dot caps. It is not encoded as four dots.

Reference mark dimensions in profile 1 are normative for rendering: inner-ring dot radius 0.14; Ring Key dot radius 0.16; Ray Key stroke width 0.22; dial data-tick width 0.15; Ring Key arc stroke width 0.32 with butt ends; Cartesian module size 0.74 with corner radius 0.16; boundary stroke width 0.65. Decoder tolerances are not yet specified, so camera support is not claimed.

The three keys are jointly required for calibration/orientation validation. A missing or inconsistent key must be a decode failure, not treated as an empty payload site.

## 3. Locator framing and integrity

The locator payload is exactly 16 arbitrary bytes. Its 26-byte data frame is:

| Offset | Length | Meaning |
|---:|---:|---|
| 0 | 2 | Magic bytes `C7 4B` |
| 2 | 1 | Format version `04` |
| 3 | 1 | Profile `01` (locator) |
| 4 | 2 | Payload length, unsigned big-endian (`00 10`) |
| 6 | 16 | Payload bytes, unchanged |
| 22 | 4 | CRC-32C of bytes 0–21, unsigned big-endian |

CRC-32C uses reflected Castagnoli polynomial `0x82F63B78`, initial value `0xFFFFFFFF`, reflected input/output, and final XOR `0xFFFFFFFF`.

The frame is encoded as a shortened systematic Reed–Solomon code RS(42,26) over GF(256), primitive polynomial `0x11D`, primitive element 2, generator roots `α^0` through `α^15`, transmitted high-degree coefficient first. Append 16 parity bytes after the frame. This code has the algebraic bound `2e + s ≤ 16` (up to 8 unknown byte errors or 16 known erasures). The implementation corrects unknown byte errors through the full `e ≤ 8` bound in deterministic test patterns. Erasure decoding is not implemented, so the `s` portion is a code capability, not a current decoder claim.

## 4. Placement and reserved geometry

The 42-byte codeword is read MSB-first, yielding 336 bits. A light mark represents 1 and background represents 0. Payload/ECC bits are mapped over eligible Cartesian and polar sites in alternating coordinate-system order: take the next Cartesian candidate, then the next polar candidate, and repeat until 336 sites are assigned. For each Cartesian candidate, compute `a=atan2(x−14.5,14.5−y)` in `[0,2π)` and assign `sector=round(72a/(2π)) mod 72` (nearest 5° sector, half ties away from zero). Within each sector sort by decreasing squared radius `(x−14.5)²+(y−14.5)²`, then by ascending row and column. Visit sectors in rounds using `sector=(37×step) mod 72`. Polar sites are grouped into five layers: dial (layer 0), then rings 0–3 (layers 1–4). Within layer `l`, slots are visited as `slot=(37×step+13×l) mod 72`, skipping reserved keys; layers are then traversed round-robin by site index. The final placement alternates one Cartesian then one polar candidate, starting with Cartesian, and appends any remainder from the non-exhausted list. The profile has 284 Cartesian and 337 polar eligible sites (621 total); unassigned sites remain background. No payload-bearing site may overlap a finder, boundary, Grid Key, Ray Key, or Ring Key.

Cartesian candidates are cell centers in rows and columns 1–27, excluding the four reserved 6 × 6 corner regions, the 2 × 2 Grid Key at rows 11–12/columns 2–3, and every cell center at radius ≤10.4 units from the symbol center. Polar candidates include four inner ring centers per available angular slot, excluding Ray Key bearings on all rings and Ring Key slots 18–25 on inner ring 3. Dial candidates occupy the same 72 angular slots in the annulus, excluding the three Ray Key bearings. A light dial data mark is a radial tick from radius 7.6 to 9.5. The three structural calibration rays occupy their reserved dial cells. The logo-safe area is the center inside radius 2.6 and contains no payload sites. No logo may cover any required structure or assigned payload/ECC mark.

The normalized geometry is shared; vector rendering uses continuous polar coordinates while Cartesian elements remain rounded square cells. The native image decoder must rectify the outer square before sampling both coordinate systems. v0.4.0 does not yet define a camera-validity envelope or decoder tolerances. Camera decoding of this rendered profile has not been implemented or validated.

## 5. Example

For payload bytes `00 01 02 … 0F`, the format-4 frame CRC-32C and RS parity are recorded in the versioned fixture [`tests/vectors/locator-v4.json`](../tests/vectors/locator-v4.json). Earlier format documents and fixtures remain as historical draft records.

## 6. Known omissions before stabilization

The next format revision must specify decoder tolerances for line width, dot diameter, arc connectivity/gap, geometric fit residuals, reflections, and distortion; full RS decode and erasures; every-version capacity accounting; and independent implementation fixtures. This draft has one locator profile only. It does not claim a general 2,048-byte profile.
