# CKey Format v0.33.0 — Profile 0 with equal-arc rings and byte-grouped placement

**Status:** draft. Format byte `0x21` (decimal 33), profile byte `0x00`, layout `CKEY_LAYOUT_STANDARD_ARC_GROUPED` (6).

This format is [Format 0.32](CKey-Format-v0.32.0.md), the equal-arc geometry, with the placement rule of [Format 0.29](CKey-Format-v0.29.0.md). Read those two documents; this one states only what differs.

## Differences from Format 0.32

* **Placement.** Format 0.29's five steps (paths, clumps, numbering, shuffle, bits), applied to Format 0.32's sites. The shuffle key is `lowbias32(0x434B4733 XOR v)` (`"CKG3"`). The rings have different slot counts; the ring path is still by ring, then by slot, so each clump of eight ring sites lies on one ring.
* **Whitening.** Format 0.32's stream (state `0x434B5733 + v`), applied by site index in the new placement order.
* **Frame.** Byte 2 = `21`.

Geometry, marks, keys, the set of sites and the capacity of every version at every level are Format 0.32's.

## Why grouped

With scattered placement a weak zone (the inner rings, a glare spot) spoils a few bits of many bytes. Grouped, it spoils the bytes that lie in it. Replaying the field frames that motivated Format 0.32 with each byte's sites in one zone, the worst block's bad bytes fell by a quarter to a third.

## Compatibility

As Format 0.32: readers before 0.1.0-build.110 cannot read this format, so encoders emit it only when asked.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v3 | `BF5ACC916EDC47B1` |
| v31 | `BD3D8D6F9C7EEADE` |
