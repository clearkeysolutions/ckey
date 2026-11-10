# CKey Format v0.30.0 — Profile 0.1 (print layout) with byte-grouped placement

**Status:** draft. Format byte `0x1E` (decimal 30), profile byte `0x00`, layout `CKEY_LAYOUT_PRINT_GROUPED` (3).

This format is [Format 0.28](CKey-Format-v0.28.0.md), the print layout, with the placement rule of [Format 0.29](CKey-Format-v0.29.0.md). Read those two documents; this one states only what differs.

## Differences from Format 0.28

* **Placement.** Format 0.29's five steps (paths, clumps, numbering, shuffle, bits), applied to Format 0.28's sites. The shuffle key is `lowbias32(0x434B4731 XOR v)` (`"CKG1"`). In the print layout the rings have different slot counts; the ring path is still by ring, then by slot.
* **Whitening.** Format 0.28's stream (state `0x434B5731 + v`), applied by site index in the new placement order.
* **Frame.** Byte 2 = `1E`.

Nothing else changes: geometry, marks, keys, the set of sites and the capacity of every version at every level are those of Format 0.28. Levels U and X are defined in Format 0.29.

## Why the print layout gains most

In the print layout every mark and gap is about a module, so a clump is a compact patch of the print. A scratch, a fold or a label edge then removes whole bytes that a reader can mark as missing. For every version at least 85% of bytes span at most 9 module pitches (v1: 23 of 27; v31: 4,809 of 4,819).

## Compatibility

As Format 0.29: readers before 0.1.0-build.86 cannot read this format, so encoders emit it only when asked.

## Conformance vectors

FNV-1a-64 over every site in placement order of (identity little-endian, printed value), for payload `"CKey renderer test"` at level M:

| Version | Hash |
|---|---|
| v3 | `06C7AEB60F425FF8` |
| v31 | `5F8333C52D1F4266` |
