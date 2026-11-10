# CKey Format v0.29.0 — Profile 0 with byte-grouped placement, and levels U and X

**Status:** draft. Format byte `0x1D` (decimal 29), profile byte `0x00`, layout `CKEY_LAYOUT_STANDARD_GROUPED` (2).

This format is [Format 0.27](CKey-Format-v0.27.0.md) with one rule changed: which site carries which codeword bit. Everything else is Format 0.27 exactly as written. [Format 0.30](CKey-Format-v0.30.0.md) makes the same change to the print layout (Format 0.28).

This document also defines two correction levels, U and X, for Formats 0.27 to 0.30.

## Why this format exists

Formats 0.27 and 0.28 scatter the codeword's bits over the field by a hash. Each byte's eight bits therefore sit in eight unrelated places. A blot, a glare spot or a logo that covers 5% of the field touches a few bits of about a third of the bytes, and one wrong bit spoils its byte.

Here each byte's eight bits sit on eight neighbouring sites. The same blot costs the bytes under it and no others. Measured with the build-86 reader (`tests/hardening_tests.cpp`): a flat disc over 5% of a v11 symbol at level M reads 6 of 6 times with grouped placement and 0 of 6 times with scattered placement.

Bytes are still spread over the whole field: a keyed shuffle decides which group of sites gets which byte, so each Reed–Solomon block has bytes in every region and local damage costs every block a little, not one block a lot.

## What does not change

* The skeleton: frame, rounded border, finders, the split top-left arc.
* The Grid, Ring and Ray Keys, and version identification from them.
* The lattice, rings and dial, mark shapes, the set of sites, and the capacity of every version at every level.
* Site identities and SplitMix32 whitening (state `0x434B5730 + v`, applied by site index in the placement order below).
* The frame layout, Reed–Solomon blocks, block interleaving and CRC-32C, except for the format byte.
* The renderer and the design system. A symbol looks the same to the eye: whitening still randomises every printed value.

## Placement (normative; replaces "Order" in Format 0.27)

Let G be the modules across for version v, and C = ⌊sites / 8⌋ the codeword length in bytes.

**1. Paths.** Each kind of site has a path.

* **Cartesian sites.** Let `half` = ⌈G / 2⌉. Rows 0 … half−1 are *top* and columns 0 … half−1 are *left*. Take the four quadrants in the order top-left, top-right, bottom-right, bottom-left. Within a quadrant, take its rows two at a time: pair k is the quadrant's rows 2k and 2k+1 (the last pair has one row if the quadrant has an odd number of rows). For an even k walk the quadrant's columns in ascending order, for an odd k in descending order. At each column take the pair's upper row, then its lower row. Lattice positions that are not sites are skipped.
* **Dial sites.** By slot, ascending.
* **Ring sites.** By ring, ascending (innermost first), then by slot, ascending.

All of these are integer rules on row, column, ring and slot indices. No geometry is evaluated, so implementations cannot disagree at a boundary.

**2. Clumps.** Cut each path into runs of 8 consecutive sites, starting at its beginning. Each run is a *clump*. Fewer than 8 sites may be left at the end of each path. Join the leftovers in the order Cartesian, dial, ring, and cut that sequence into runs of 8 in the same way; these are *mixed* clumps (there are at most two). At most 7 sites then remain. They carry whitening only, as in Format 0.27, and come last in the placement order.

**3. Numbering.** Number the clumps g = 0, 1, … in the order: Cartesian clumps, dial clumps, ring clumps, mixed clumps. There are exactly C of them.

**4. Shuffle.** Let key = `lowbias32(0x434B4730 XOR v)` (`"CKG0"`). Sort the clump numbers by the pair (`lowbias32(g XOR key)`, g), ascending. Codeword byte j uses the clump at position j of that sorted list.

**5. Bits.** Bit 7 (the most significant) of byte j is the first site of its clump's run, bit 0 the last.

The placement order is therefore: the 8 sites of byte 0's clump, the 8 sites of byte 1's clump, and so on, then the remaining sites. Site i < 8C carries codeword bit i, most significant bit first, XORed with whitening bit i.

`lowbias32` is the hash defined in Format 0.25.

**Locality (informative).** A clump's sites are of one kind except in the mixed clumps. For every version, at least 94% of bytes in this format (v1: 50 of 53; v31: 6,442 of 6,451) span at most 9 module pitches; the rest are clumps that straddle a gap in a path, such as the edge of a finder clearance.

## Frame

As Format 0.27, with byte 2 = `1D`. Byte 5 (level) takes the values 0 to 5 (see the next section).

## Levels U and X (Formats 0.27 to 0.30)

The parity percentage `pct` in Format 0.27's block rule gains two values. The rule itself, 2·⌊(pct·n_b + 100) / 200⌋ parity bytes per block (minimum 2, and at most n_b − 1), is unchanged.

| Level | Code | pct | Corrects, unknown bad bytes | Corrects, known-missing bytes (erasures) |
|---|---|---|---|---|
| L | 0 | 14 | about 7% | 14% |
| M | 1 | 28 | about 14% | 28% |
| Q | 2 | 44 | about 22% | 44% |
| H | 3 | 60 | about 30% | 60% |
| U (Ultra) | 4 | 80 | about 40% | 80% |
| X (Extreme) | 5 | 95 | about 47.5% | 95% |

U and X are the same code with more parity. They add no decoder path. A reader that predates them (before 0.1.0-build.86) rejects symbols at these levels, because it never tries their parity.

**Capacity, in payload bytes (standard layout).** Each symbol also spends 12 bytes on its header and CRC.

| Version (modules across) | L | M | Q | H | U | X |
|---|---|---|---|---|---|---|
| v1 (29) | 33 | 27 | 17 | 9 | 0 | 0 |
| v4 (36) | 66 | 52 | 38 | 24 | 6 | 0 |
| v8 (47) | 159 | 131 | 99 | 67 | 27 | 0 |
| v11 (58) | 276 | 228 | 176 | 124 | 56 | 4 |
| v16 (82) | 607 | 505 | 391 | 277 | 133 | 25 |
| v21 (116) | 1,289 | 1,085 | 835 | 593 | 293 | 65 |
| v26 (164) | 2,684 | 2,242 | 1,748 | 1,244 | 620 | 152 |
| v31 (232) | 5,555 | 4,619 | 3,579 | 2,585 | 1,285 | 303 |

A capacity of 0 means the version cannot carry a payload at that level; an encoder refuses it. In the print layout, U carries up to 945 bytes (v31) and X up to 223; X carries nothing below v15.

U roughly halves H's payload at the same size. X keeps about 5% of L's. A 64-byte payload needs v8 at H, v13 at U and v21 at X, and a denser symbol needs more pixels to read, so X suits short identifiers on large prints or harsh surfaces, not general payloads.

## Reading (informative)

A reader cannot know the placement before it reads the frame, and the format byte is inside the frame. It does not need to: both placements use the same sites, so the reader samples the sites once and assembles the codeword under each placement. The frame's first bytes (`C7 4B`, format, profile, version) are readable before any correction, because the code is systematic, and usually show at once which placement is right. Reed–Solomon and CRC-32C make a wrong guess fail safely.

Grouped placement is what makes **erasure decoding** pay. Reed–Solomon corrects any pattern with 2 × (unknown bad bytes) + (known-missing bytes) ≤ parity. When a reader can tell that a region is unreadable (washed out by glare, covered, or outside the image), it marks the bytes there as missing, and each costs one parity byte instead of two. With scattered placement nearly every byte has a bit in any damaged region, so there is nothing to gain.

## Compatibility

Readers before 0.1.0-build.86 cannot read Format 0.29 or 0.30, or levels U and X. Encoders therefore emit them only when asked. Formats 0.27 and 0.28 remain current, and readers continue to decode them.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v2 | `DD98DC05A83AB143` |
| v31 | `B10ECE43C83BDA6D` |

`tests/hardening_tests.cpp` checks both, the capacity table above, and for every version that the placement is a permutation of Format 0.27's sites and that each Reed–Solomon block has bytes in all four quadrants.
