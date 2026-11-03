# CKey locator format v0.2.0 (draft)

Status: experimental implementation profile. Wire and geometry constants are versioned as `format=2, profile=1`; interoperability is not promised until decoder fixtures pass in independent implementations.

## 1. Coordinate system and symbol shape

The canonical symbol field is 29 × 29 module units. The origin is the upper-left; x increases right and y increases down. Its center is `(14.5, 14.5)`. A 0.65-unit rounded square boundary runs from `(0.5,0.5)` to `(28.5,28.5)` with radius 3.5. The four corner finders are nested open quarter-arcs, not solid QR-like blocks. The upper-left finder uses outer arc `M 0.8 5.2 A 4.4 4.4 0 0 1 5.2 0.8` at stroke width 0.85, inner arc `M 1.6 4.4 A 2.8 2.8 0 0 1 4.4 1.6` at stroke width 0.48, both round-capped, and an orientation dot of radius 0.38 at `(3,3)`. The other three finders are reflections/rotations of that construction across the symbol center. Their 5 × 5 bounding regions remain reserved from payload. The outer boundary and corner finder geometry are structural and take precedence over other layers. These dimensions remain fixed for profile 1.

The polar angle `a` is clockwise from 12:00. There are 72 angular slots per turn, each exactly 5 degrees. Polar point `(ring, slot)` has radius `4.5 + ring` for rings 0 through 5, and center

`x = 14.5 + radius × sin(2π × slot / 72)`  
`y = 14.5 − radius × cos(2π × slot / 72)`.

The symbol is monochrome. The reference renderer uses a dark background and light marks; a decoder must normalize either contrast polarity from image evidence. Color never changes interpretation. Reference SVG colors `#050505` and `#f4f1e8` are decorative only.

## 2. Permanent calibration keys

These are reserved structures and cannot be payload sites.

* **Grid Key:** At Cartesian column 2, rows 10 through 17, the fixed word `10110100` is rendered as centered circular marks in the corresponding cells. It calibrates Cartesian pitch and verifies local grid alignment.
* **Ray Key:** At slot 0 (12:00), a ray reaches 100% of the 9.5-unit polar radius. At slot 54 (9:00) it reaches 50%, and the immediately adjacent slot 55 (9:05) reaches 75%. The angular slots and radius are normative.
* **Ring Key:** On ring 5, slots 18 and 19 are individual dots; slot 20 is explicitly empty; slot 21 is one individual dot; slots 22–25 are one connected, continuous four-slot arc. A dot is a circular mark centered in one angular slot. An arc is one connected stroke spanning four slot intervals, with no internal gaps or dot caps. It is not encoded as four dots.

Reference mark dimensions in profile 1 are normative for rendering: Grid Key dot radius 0.29 units; payload polar rounded-square module size 0.34; Ring Key dot radius 0.16; Ray Key stroke width 0.22; Ring Key arc stroke width 0.32 with butt ends; Cartesian module size 0.74 with corner radius 0.16; rounded-square boundary stroke width 0.65. A future decoder profile must specify measurable tolerances around these nominal dimensions before claiming camera support; v0.2.0 does not yet define those tolerances.

The three keys are jointly required for calibration/orientation validation. A missing or inconsistent key must be a decode failure, not treated as an empty payload site.

## 3. Locator framing and integrity

The locator payload is exactly 16 arbitrary bytes. Its 26-byte data frame is:

| Offset | Length | Meaning |
|---:|---:|---|
| 0 | 2 | Magic bytes `C7 4B` |
| 2 | 1 | Format version `01` |
| 3 | 1 | Profile `01` (locator) |
| 4 | 2 | Payload length, unsigned big-endian (`00 10`) |
| 6 | 16 | Payload bytes, unchanged |
| 22 | 4 | CRC-32C of bytes 0–21, unsigned big-endian |

CRC-32C uses reflected Castagnoli polynomial `0x82F63B78`, initial value `0xFFFFFFFF`, reflected input/output, and final XOR `0xFFFFFFFF`.

The frame is encoded as a shortened systematic Reed–Solomon code RS(42,26) over GF(256), primitive polynomial `0x11D`, primitive element 2, generator roots `α^0` through `α^15`, transmitted high-degree coefficient first. Append 16 parity bytes after the frame. This code has the algebraic bound `2e + s ≤ 16` (up to 8 unknown byte errors or 16 known erasures). The implementation corrects unknown byte errors through the full `e ≤ 8` bound in deterministic test patterns. Erasure decoding is not implemented, so the `s` portion is a code capability, not a current decoder claim.

## 4. Placement and reserved geometry

The 42-byte codeword is read MSB-first, yielding 336 bits. A light mark represents 1 and background represents 0. Payload/ECC bits are mapped over eligible Cartesian and polar sites in alternating coordinate-system order: take the next Cartesian candidate, then the next polar candidate, and repeat until one list is exhausted; then continue through the remaining list. Cartesian eligible cells are in row-major order; polar eligible sites are in ring-major/slot-major order. The profile has 316 Cartesian and 406 polar eligible sites (722 total); remaining sites are unassigned background. No payload-bearing site may overlap a finder, boundary, Grid Key, Ray Key, or Ring Key.

Cartesian candidates are cell centers in rows and columns 1–27, excluding the four reserved 5 × 5 corner regions, the 8-cell Grid Key, and every cell center at radius ≤10.4 units from the symbol center. Polar candidates are all ring/slot centers except the three Ray Key bearings on every ring and Ring Key slots 18–25 on ring 5. Candidates beyond bit 335 remain unassigned background. The logo-safe area is **not** implicitly the polar center: no logo may cover any required structure or assigned payload/ECC mark. A future profile may define explicit reserved-empty/logo-safe sites only after damage limits are specified.

The normalized geometry is shared; vector rendering uses continuous polar coordinates while Cartesian elements remain rounded square cells. The native image decoder must rectify the outer square before sampling both coordinate systems. v0.2.0 does not yet define a camera-validity envelope or decoder tolerances. This renderer is a visual reference matching the supplied design direction; camera decoding of this rendered profile has not been implemented or validated.

## 5. Example

For payload bytes `00 01 02 … 0F`, the format-2 frame ends with CRC-32C `F6 4C DE C4`; RS parity is `2B 54 54 79 A2 87 3F BF 57 F7 9E 3B 72 6B BE 6A`. The fixture is in [`tests/vectors/locator-v2.json`](../tests/vectors/locator-v2.json). The v0.1.0 document and `locator-v1.json` remain as historical draft records.

## 6. Known omissions before stabilization

The next format revision must specify decoder tolerances for line width, dot diameter, arc connectivity/gap, geometric fit residuals, reflections, and distortion; full RS decode and erasures; every-version capacity accounting; and independent implementation fixtures. This draft has one locator profile only. It does not claim a general 2,048-byte profile.
