# CKey Format v0.32.0 — Profile 0 with equal-arc rings

**Status:** draft. Format byte `0x20` (decimal 32), profile byte `0x00`, layout `CKEY_LAYOUT_STANDARD_ARC` (5).

This format is [Format 0.27](CKey-Format-v0.27.0.md) with the ring zone's slot counts changed and the Ring Key moved. Everything else is Format 0.27 exactly as written. [Format 0.33](CKey-Format-v0.33.0.md) applies Format 0.29's byte-grouped placement to this geometry.

## Why this format exists

Format 0.27 gives every ring the dial's slot count D, at equal angles. Arc spacing therefore shrinks with radius: at v21 (D = 288, 17 rings from r = 3.40 to 6.84) the innermost ring's marks are 0.074 units apart, against 0.149 on the outermost ring and a module pitch of 0.25. On screens and in blur the inner rings fail first.

Field evidence (Open CKey Scanner on an iPhone (model iPhone18,3), a v21 level M code shown pixel-perfect on a monitor, build 108, per-site heatmap from the scan report): over twenty kept frames, ring errors fell steadily with radius, from about 21% inside r = 4.3 to about 1% on the outermost rings, while the grid read at 0.1–0.4%. In frames where only 4% of all sites read wrong, the rings inside r = 4.3 alone spoiled more than half of the bad codeword bytes, the worst Reed–Solomon block had 77–86 bad bytes against 35 correctable, and the code did not read for a further five seconds. Replaying those frames with the inner rings reading as well as the rest, it reads five seconds sooner.

## Ring slots (normative; replaces Format 0.27's ring slot count)

Let D be the version's dial slot count, K its ring count, and r_k = 3.4 + 0.86 p k the radius of ring k (k = 0 the innermost), as in Format 0.27. Ring k has

  S_k = max(8, 8 ⌊D r_k / r_{K-1} / 8 + 10⁻⁹⌋)

slots at equal angles, slot s at angle 2π s / S_k clockwise from 12:00. The outermost ring keeps S_{K-1} = D. No ring's marks are closer together than the outermost ring's.

**Ray Key bearings.** Format 0.27 leaves ring slots empty on the three Ray Key bearings (dial slots 0, 3D/4 and 3D/4 − 2). Here each ring leaves empty, for each bearing b, the slot ⌊(2 b S_k + D) / (2D)⌋ mod S_k (the nearest by angle). On the outermost ring these are Format 0.27's slots.

## Ring Key (normative)

The Ring Key's pattern is Format 0.27's, on the outermost ring, starting at slot D/2 (6:00) instead of D/4 (3:00). A reader's version fit therefore tells this format from Format 0.27, whose ring slots at 6:00 carry data.

## Placement and whitening

Format 0.27's rule, with its keys for this family: placement `lowbias32(0x434B5033 XOR v)` (`"CKP3"`), whitening state `0x434B5733 + v` (`"CKW3"`). Sites are generated as in Format 0.27: grid, then dial by slot, then rings by ring and slot.

## Capacity

About 10% below Format 0.27 for large versions, more for small ones. Payload bytes at level M (Format 0.27 in brackets):

| v | M | v | M | v | M | v | M |
|---|---|---|---|---|---|---|---|
| 1 | 21 (27) | 9 | 133 (150) | 17 | 532 (597) | 25 | 1,710 (1,911) |
| 2 | 32 (36) | 10 | 166 (188) | 18 | 619 (696) | 26 | 1,990 (2,242) |
| 3 | 37 (41) | 11 | 202 (228) | 19 | 702 (797) | 27 | 2,289 (2,580) |
| 4 | 46 (52) | 12 | 231 (261) | 20 | 823 (927) | 28 | 2,649 (2,985) |
| 5 | 54 (62) | 13 | 288 (324) | 21 | 959 (1,085) | 29 | 3,096 (3,476) |
| 6 | 75 (87) | 14 | 315 (357) | 22 | 1,094 (1,234) | 30 | 3,541 (4,007) |
| 7 | 90 (102) | 15 | 389 (441) | 23 | 1,282 (1,441) | 31 | 4,100 (4,619) |
| 8 | 115 (131) | 16 | 448 (505) | 24 | 1,505 (1,701) | | |

## Appearance

The frame, finders, grid, dial and mark shapes are Format 0.27's. Inner rings carry fewer, longer marks (at v21 the innermost ring has 136 slots instead of 288, 0.157 units apart), so the ring band has one grain from the inside out.

## Compatibility

Readers before 0.1.0-build.110 cannot read this format. From 0.1.0-build.114 the SDKs' automatic layout (`CKEY_LAYOUT_AUTO`, their default) emits Format 0.33 for versions 8 to 31 and Format 0.27 below; Format 0.32 itself is emitted only when asked. A reader that supports it tries this family beside Formats 0.27 to 0.31 and reports layout 5.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v3 | `9A0D790928793DB3` |
| v31 | `296AC7423C01B6B4` |
