# CKey Format v0.28.0 — Profile 0.1: the print-robust layout

Status: draft. Added alongside Profile 0 (Format 0.27), which is unchanged.

Encoders emit frame format byte `0x1C` (decimal 28), profile byte `0x00`. The layout family is the print family (`CKEY_LAYOUT_PRINT`).

## Why this layout exists

Profile 0's polar field is finer than its modules. Rays are 0.2 module wide, ring dots 0.4 module across, and ring slots are as little as 0.3 module apart on the inner rings. Printed small, those marks are the first to go:

- **Dark ink on paper:** ink spread merges neighbouring ring dots.
- **Inked background:** spread closes the light rays.

Profile 0.1 keeps every mark and every gap at **about one module or more**. The module can then shrink to whatever the printer and camera allow. In print simulations, the same payload prints at roughly half Profile 0's width (see "Measured" below).

## Differences from Format 0.27

Everything not listed here is identical to Format 0.27: the skeleton, frame, finders, Grid Key, version table G(v), safe-area rule, Reed–Solomon framing and levels, header layout, and interleaving.

| | Format 0.27 (P0) | Format 0.28 (P0.1) |
|---|---|---|
| Ring radial pitch | 0.86p | **1.0p** |
| Rings R(v) | table in 0.27 | **2 3 3 3 4 4 4 5 5 6 7 7 8 8 9 10 11 12 13 14 15 16 17 19 20 22 24 25 27 29 32** (= ⌊(4.2 − 2.4p)/p⌋ + 1) |
| Slots per ring | one count S for all rings | **equal arc:** ring k at radius r gets 8·⌊2πr/(8p)⌋ slots, so dots are at least one module apart on every ring |
| Ring dot radius | 0.2p | **0.3p** |
| Dial slots | S (≈ 72/p) | **8·⌊2π·7.6/(8p)⌋** (≈ 48/p; rays at least one module apart) |
| Dial ray width | 0.2p | **0.35p** |
| Long / short ray | 2.2p / 1.0p | **2.4p** / 1.0p |
| Data ray outer ink (for the safe area) | 7.6 + 2.3p | 7.6 + 2.575p |
| Ray-Key bearings on rings | ring slots cleared on the three Ray Key slots | **not cleared**: rings have their own slot counts |
| Ring Key | 16 slots: dots K, K+2, K+6; run K+7.5…K+15.5 | **8 slots:** dots K, K+2; run K+3.5…K+7.5 |
| Grid Key, v1 | row 10 | **row 9**, giving the longer rays a pitch of clearance |
| Placement key / whitening seed | `0x434B5030` / `0x434B5730` + v | **`0x434B5031` / `0x434B5731` + v** |
| Frame format byte | `1B` | **`1C`** |

**Normative and derived values:**

- **The ring-count table is normative.** For some versions it lands exactly on an integer boundary, so implementations must use the table.
- **Slot counts are derived** and decided with at least 0.002 slack, so independent implementations agree.

**Ink-to-ink gaps** in every version: ring dots at least 0.39p along and across rings, and dial rays at least 0.6p apart. The test suite checks both.

## Capacity (payload bytes)

| v | Modules across | L | M | Q | H |
|---|---|---|---|---|---|
| 1 | 29 | 11 | 7 | 3 | 0 |
| 2 | 31 | 21 | 17 | 9 | 3 |
| 3 | 33 | 31 | 23 | 15 | 7 |
| 4 | 36 | 35 | 27 | 19 | 9 |
| 5 | 38 | 46 | 36 | 26 | 16 |
| 6 | 41 | 62 | 50 | 36 | 22 |
| 7 | 44 | 75 | 61 | 45 | 29 |
| 8 | 47 | 98 | 80 | 60 | 40 |
| 9 | 50 | 116 | 96 | 72 | 48 |
| 10 | 54 | 152 | 124 | 94 | 64 |
| 11 | 58 | 191 | 157 | 119 | 81 |
| 12 | 62 | 221 | 185 | 141 | 97 |
| 13 | 67 | 274 | 230 | 174 | 122 |
| 14 | 71 | 295 | 245 | 189 | 131 |
| 15 | 77 | 367 | 303 | 235 | 163 |
| 16 | 82 | 433 | 361 | 277 | 193 |
| 17 | 88 | 518 | 428 | 332 | 232 |
| 18 | 94 | 608 | 506 | 392 | 278 |
| 19 | 101 | 694 | 574 | 446 | 314 |
| 20 | 108 | 814 | 678 | 526 | 374 |
| 21 | 116 | 958 | 798 | 618 | 438 |
| 22 | 124 | 1076 | 900 | 696 | 496 |
| 23 | 133 | 1272 | 1056 | 816 | 588 |
| 24 | 143 | 1504 | 1266 | 978 | 692 |
| 25 | 153 | 1705 | 1423 | 1103 | 783 |
| 26 | 164 | 2017 | 1677 | 1299 | 919 |
| 27 | 176 | 2318 | 1944 | 1504 | 1064 |
| 28 | 188 | 2655 | 2235 | 1719 | 1225 |
| 29 | 202 | 3056 | 2576 | 1990 | 1420 |
| 30 | 216 | 3564 | 2986 | 2306 | 1648 |
| 31 | 232 | 4123 | 3453 | 2679 | 1919 |

## Reading

The decoder tests both layout families' key patterns (Grid, Ring and Ray Keys at each version's own pitch), so the family is identified the same way the version is. The frame's format byte confirms it. In the test suite, all 31 Profile 0.1 versions decode at 6 px per module and are identified as Profile 0.1.

## Measured (simulation, informative)

The print model is binary device dots, ink spread (dot gain), paper blur, then a camera of a given resolution, then the decoder. The table gives the smallest width that decodes reliably at level M, P0 → P0.1:

| Payload | Old inkjet, plain paper | Inkjet, photo paper | Weaker camera (12 px/mm), plain paper |
|---|---|---|---|
| 8 B | 0.38 → 0.19 in | 0.21 → ≤ 0.15 in | 0.47 → 0.24 in |
| 30 B | 0.42 → 0.24 in | 0.29 → ≤ 0.15 in | 0.56 → 0.29 in |
| 120 B | 0.68 → 0.32 in | 0.38 → 0.21 in | 0.68 → 0.42 in |
| 400 B | 0.91 → 0.56 in | 0.62 → 0.38 in | 1.22 → 0.75 in |
| 1,200 B | 1.47 → 0.91 in | 1.11 → 0.56 in | 1.78 → 1.22 in |
| 2,400 B | 2.37 → 1.34 in | 1.34 → 0.83 in | 2.61 → 1.62 in |

The model's parameters:

- **Old inkjet, plain paper:** 600 dpi, 0.045 mm spread.
- **Photo paper:** 1,200 dpi, 0.015 mm spread.
- **Camera:** 23 px/mm with 0.7 px blur.

These are estimates. `ckey print-sheet` produces the calibration ladder that replaces them with measurements.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v3 | `1908543413F369B8` |
| v31 | `684C56624D3558A8` |
