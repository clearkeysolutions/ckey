# CKey Format v0.27.0 — Profile 0: the scalable family

Status: draft. Added alongside Profiles 1–3; none of them changes.

Encoders emit frame format byte `0x1B` (decimal 27), profile byte `0x00`.

## Why this profile exists

Profiles 1–3 are three fixed points: 16 bytes, up to 200 bytes, and 256–2,048 bytes. Between them, capacity jumps about 12× and then 16× in area. A 33-character URL jumped straight from Profile 1's look to Profile 2's density.

Profile 0 replaces those jumps with 31 versions. All versions share one skeleton (frame, finders, polar centre and dial position). Each version has a finer module pitch than the last, about 7% finer per step. Capacity rises smoothly from 27 bytes (v1) to 4,619 bytes (v31) at level M. No step more than doubles it, and most steps add 7–30%.

**The fixed calibration keys identify the version.** The Grid, Ring and Ray Keys are drawn at each version's own module pitch and slot pitch, so a reader recognizes the version from them. The error-corrected frame then repeats the version number.

## Version table (normative)

The canonical field is `[0,29]²`, exactly as in Profiles 1 and 2. Version v has G(v) modules across and module pitch `p = 29 / G(v)`.

The following per-version integers are normative. They approximate a geometric series, but implementations must use the table rather than recompute it, because v9 and v11 fall on rounding ties.

| Quantity | Values for v = 1…31 |
|---|---|
| G (modules across) | 29 31 33 36 38 41 44 47 50 54 58 62 67 71 77 82 88 94 101 108 116 124 133 143 153 164 176 188 202 216 232 |
| S (slots per ring and dial) | 72 80 80 88 96 104 112 120 128 136 144 152 168 176 192 200 216 232 248 272 288 304 328 352 376 408 440 464 504 536 576 |
| R (rings) | 3 3 3 4 4 5 5 6 6 7 8 8 9 10 11 12 13 14 15 16 17 19 20 22 23 25 27 29 32 34 37 |
| Grid Key column c₀ | 2 2 2 3 3 3 4 4 4 5 5 5 6 6 7 7 8 9 9 10 11 12 13 14 15 16 17 18 20 21 23 |
| Grid Key row r₀ | 10 12 13 14 15 16 17 18 20 21 23 25 27 28 31 33 35 38 41 44 47 50 54 58 62 67 72 77 83 88 95 |

**Derived values:**

- **G, S and R** follow G = round(29·2^((v−1)/10)), S = 8·round(9G/29), and R = ⌊(4.2 − 2.38p)/(0.86p)⌋ + 1.
- **The Grid Key position** follows c₀ = round(3G/29) − 1 and r₀ = round(12G/29) − 1. The one exception is v1's row, raised to 10 so the key keeps one pitch of clearance from the dial.
- **Version 11** has exactly Profile 2's density, and **version 31** has Profile 3's.

## Geometry

**Fixed at every version** (identical to Profile 2):

- **Frame:** a rect at (0.5, 0.5), 28 × 28, rx 3.5, stroke 0.65.
- **Finders:** an arc of radius 2.3 about each corner inset 4, stroke 0.8, round caps. The top-left arc is split about 225° with a 0.95 clear gap.
- **Polar anchors:** ring 0 at r = 3.4, and dial rays starting at r = 7.6.

**Scaled by the pitch p:**

| Element | Geometry |
|---|---|
| Cartesian lattice | Cells of pitch p, centred at ((col+½)p, (row+½)p) for col, row ∈ [0, G) |
| Cartesian module | Rect 0.68p × 0.68p, rx 0.16p |
| Rings | k = 0…R−1 at r = 3.4 + 0.86p·k; dots of radius 0.2p; runs drawn as in Format 0.25 with stroke 0.4p |
| Dial | One ray per non-key slot, from r = 7.6 outward, width 0.2p, round caps: long 2.2p (printed 1), short 1.0p (printed 0) |
| Grid Key | Four modules in cells (c₀, r₀), (c₀+1, r₀), (c₀, r₀+1), (c₀+1, r₀+1) |
| Ring Key | On ring R−1 at slots K = S/4: dots at K, K+2, K+6; one run from slot position K+7.5 to K+15.5; slots K…K+15 reserved |
| Ray Key | Strokes from r = 7.6 to 7.6 + 3.4p·f at slots 0, 3S/4, 3S/4−2, with f = 1, 0.5, 0.75; width 0.44p |

Slot k of S lies at angle 2πk/S, clockwise from 12:00. The SVG viewBox is `−1 −1 31 31`.

## Safe area and sites

**Cartesian sites.** A cell is a site when its slack under the Format 0.25 rule is strictly positive, with:

- module half-size 0.34p;
- frame and finder clearance max(0.375, p);
- Grid Key clearance p;
- radial clearance p, from the data-ray outer ink edge at r = 7.6 + 2.3p and from each Ray Key stroke (half-width 0.22p).

**Polar sites.**

- **Dial:** every slot except the three Ray Key slots.
- **Rings:** every slot on every ring except the three Ray Key slots. On ring R−1, slots K…K+15 (the Ring Key) are also excluded.

**Order and value.**

- **Identities:** sites use Format 0.25's identities, with kinds 0 (Cartesian), 1 (dial) and 2 (ring).
- **Order:** sites are sorted by `lowbias32(id XOR lowbias32(0x434B5030 XOR v))`.
- **Whitening:** the stream starts from SplitMix32 state `0x434B5730 + v`.

**Codeword.** The codeword length is C = ⌊sites / 8⌋ bytes. Site i < 8C carries codeword bit i, most significant bit first; at most 7 trailing sites carry whitening only. There is no fill waste: every other site carries data or parity.

## Frame and error correction

**Frame layout:**

| Bytes | Content |
|---|---|
| 0–1 | `C7 4B` |
| 2 | Format `1B` |
| 3 | Profile `00` |
| 4 | Version v |
| 5 | Level (0 L, 1 M, 2 Q, 3 H) |
| 6–7 | Payload length, u16 big-endian |
| 8 … 8+L−1 | Payload |
| next 4 | CRC-32C over everything before it |
| rest | Format 0.17 SplitMix32 visual fill, up to the data capacity K |

**Reed–Solomon blocks.** The C codeword bytes form B = ⌈C / 255⌉ blocks. Block b has n_b = ⌊C/B⌋ + (1 if b < C mod B), so longer blocks come first. Its parity is 2·⌊(pct·n_b + 100) / 200⌋, minimum 2, with pct = 14 (L), 28 (M), 44 (Q) or 60 (H). Each level therefore corrects about 7, 14, 22 or 30% of each block.

The RS code is the project's usual one (primitive polynomial `0x11D`, roots α⁰…). Frame bytes fill the blocks' data portions in order. The coded blocks are then byte-interleaved: index j outer, block b inner, skipping blocks shorter than j.

**Choosing a version.** The encoder chooses the smallest version whose capacity (K − 12 bytes) fits the payload, unless the caller names a version.

## Capacity (payload bytes)

| v | Modules across | Pitch | L | M | Q | H |
|---|---|---|---|---|---|---|
| 1 | 29 | 1.0000 | 33 | 27 | 17 | 9 |
| 2 | 31 | 0.9355 | 44 | 36 | 24 | 14 |
| 3 | 33 | 0.8788 | 51 | 41 | 29 | 17 |
| 4 | 36 | 0.8056 | 66 | 52 | 38 | 24 |
| 5 | 38 | 0.7632 | 76 | 62 | 46 | 28 |
| 6 | 41 | 0.7073 | 105 | 87 | 65 | 43 |
| 7 | 44 | 0.6591 | 124 | 102 | 76 | 52 |
| 8 | 47 | 0.6170 | 159 | 131 | 99 | 67 |
| 9 | 50 | 0.5800 | 180 | 150 | 114 | 78 |
| 10 | 54 | 0.5370 | 224 | 188 | 144 | 100 |
| 11 | 58 | 0.5000 | 276 | 228 | 176 | 124 |
| 12 | 62 | 0.4677 | 313 | 261 | 199 | 139 |
| 13 | 67 | 0.4328 | 388 | 324 | 248 | 172 |
| 14 | 71 | 0.4085 | 429 | 357 | 273 | 195 |
| 15 | 77 | 0.3766 | 525 | 441 | 339 | 237 |
| 16 | 82 | 0.3537 | 607 | 505 | 391 | 277 |
| 17 | 88 | 0.3295 | 717 | 597 | 461 | 325 |
| 18 | 94 | 0.3085 | 840 | 696 | 544 | 384 |
| 19 | 101 | 0.2871 | 947 | 797 | 617 | 437 |
| 20 | 108 | 0.2685 | 1119 | 927 | 723 | 513 |
| 21 | 116 | 0.2500 | 1289 | 1085 | 835 | 593 |
| 22 | 124 | 0.2339 | 1486 | 1234 | 954 | 688 |
| 23 | 133 | 0.2180 | 1713 | 1441 | 1121 | 791 |
| 24 | 143 | 0.2028 | 2021 | 1701 | 1321 | 941 |
| 25 | 153 | 0.1895 | 2285 | 1911 | 1493 | 1053 |
| 26 | 164 | 0.1768 | 2684 | 2242 | 1748 | 1244 |
| 27 | 176 | 0.1648 | 3090 | 2580 | 2010 | 1440 |
| 28 | 188 | 0.1543 | 3563 | 2985 | 2305 | 1649 |
| 29 | 202 | 0.1436 | 4156 | 3476 | 2716 | 1916 |
| 30 | 216 | 0.1343 | 4769 | 4007 | 3097 | 2217 |
| 31 | 232 | 0.1250 | 5555 | 4619 | 3579 | 2585 |

## Reading (informative)

1. **Find borders.** `ckey_find_borders_gray8` finds border candidates and ranks them by how well the shared skeleton fits.
2. **Align the frame.** `ckey_refine_quad_gray8(…, CKEY_FIELD_SCALABLE)` aligns on both edges of the frame stroke, with the sample separation shrinking from about 6 px to 0.5 px as the search step shrinks (coarse-to-fine). It then picks the version whose keys fit best and fine-tunes against them. In simulated 4K frames this resolves v31 corners to 0.06–0.22 modules.
3. **Identify the version and decode.** `ckey_decode_scalable_gray8` gates each orientation on the skeleton. It then gates each version on its own key pattern: the Grid Key on that version's lattice, the Ring Key at its slot pitch, and the Ray Keys. The decoder fully samples only versions that pass, best first. Reed–Solomon, the frame's version byte and CRC-32C decide.

**Measured on synthetic renders:** all 31 versions decode at 6 px per module clean and at 8 px per module after a 3×3 blur. Full 1080p/4K camera frames with clutter, rotation up to 33°, 8% keystone, blur and inverted polarity also decode.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v2 | `B97EF7ACABD3C8D7` |
| v31 | `C7DF4DFD1C36458B` |
