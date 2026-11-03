# CKey locator format v0.14.0 (draft)

Status: experimental reference profile. Encoder output uses `format=14` (`0E`)
and `profile=1`. Historic locator formats 8 and 11--13 remain decoder-readable.

## Corner finders

The field is 29 by 29 units with a continuous 0.65-unit rounded-square border
from `(0.5,0.5)` to `(28.5,28.5)`, radius 3.5. Each finder is **one** broad,
round-capped arc inside that border. Its local top-left centre is `(4.0,4.0)`,
radius `2.30`, width `0.80`; its reflected instances occupy the other corners.
The arc follows the upper-left exterior quadrant from `(1.70,4.0)` to
`(4.0,1.70)`, so its open/concave mouth faces down-right toward the polar ring
matrix. The other three corner arcs are continuous. The top-left arc is split
at 225 degrees with a cap-compensated `0.95`-unit visible gap, which is the
orientation signature. All 6 by 6 corner envelopes are reserved.

Profile 2 and Profile 3 use this same normalized construction: one inboard
arc per rounded corner, a continuous outer border, and the top-left split.
Their geometric scale changes with their physical field; it does not change
the one-arc semantic or its orientation.

## Remaining rules

Grid, Ray, and Ring Keys; 72-slot polar rings and ray band; payload placement;
and the 16-byte locator capacity are unchanged. The frame is
`C7 4B 0E 01 00 10`, payload, then CRC-32C; it is systematic shortened
RS(77,26) over GF(256), polynomial `0x11D`, with 51 parity bytes and a
25-unknown-byte-error bound. An image reader uses structural key/finder
evidence to fit geometry but accepts a payload only after RS and CRC checks.

The normative vector is [`locator-v14.json`](../tests/vectors/locator-v14.json).
