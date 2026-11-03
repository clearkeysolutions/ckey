# CKey locator format v0.11.0 (draft)

Status: experimental reference format. The emitted locator frame has `format=11`
(`0B`) and `profile=1`. Format 8 remains a historical decoder-compatibility
format; it is not emitted by this revision.

## Geometry

The canonical field is 29 by 29 units with origin at the upper-left and centre
`(14.5,14.5)`. A rounded-square structural border runs from `(0.5,0.5)` to
`(28.5,28.5)`, radius 3.5, width 0.65. Coordinates are shared by renderer and
decoder; colour is decorative only.

Each corner finder is an **inset, edge-oriented three-arc set**, not a
border-sized leaf. In local top-left coordinates its centre is `(2.75,2.75)`.
The three circular centerline radii are `1.45`, `2.20`, and `2.95`; all have
width `0.42` and rounded caps. Top-right, bottom-right, and bottom-left are
exact reflections and each has three continuous quarter arcs. The top-left
finder has the same three arcs, but every arc is divided at the 45-degree
diagonal with a `0.85`-unit visible clear gap (including cap compensation).
This split is the orientation signature. The physical 6 by 6 corner envelope
remains non-payload, so data never touches a finder.

The polar field remains four dot rings at radii `3.6 + .8r`, `r=0...3`, and a
separate ray band from radius 7.6 to 9.5. There are 72 equally-spaced angular
slots, clockwise from 12:00. The Grid Key is a 2 by 2 Cartesian block at rows
11--12 / columns 2--3. Ray Key slots 0, 54, 55 have lengths 100%, 50%, 75%.
The outer ring's Ring Key is dots at slots 18,19,21; empty at 20; and one
round-capped connected arc in slots 22--25. Ring dots and arc width are 0.30;
the arc's visible length is exactly four slots. These structures are reserved.

## Framing and placement

The payload is exactly 16 bytes. Frame bytes are `C7 4B 0B 01 00 10`, payload,
then CRC-32C over bytes 0--21. The 26-byte frame is systematic shortened
RS(77,26) over GF(256), primitive polynomial `0x11D`, primitive element 2,
roots `alpha^0...alpha^50`, high-degree-first coefficients, with 51 appended
parity bytes. It corrects up to 25 unknown byte errors. CRC-32C is separate
integrity detection, not error correction.

The 616 codeword bits are placed MSB-first by the same alternating
Cartesian/polar sequence specified in v0.8.0: Cartesian sites are angularly
interleaved; all 69 data ray slots precede round-robin inner ring sites. A
light mark represents one. The Grid, Ray, Ring, boundary, finders, and center
radius 10.4 are reserved. The central region inside radius 3.3 is logo-safe;
no logo may overwrite a structural, payload, or parity site.

## Reader requirements

An image reader rectifies a caller-supplied cyclic quadrilateral, normalizes
monochrome polarity from the permanent keys, and must require Grid/Ray/Ring
evidence, RS correction, and CRC-32C. The inset finder arcs are included as
continuous localization evidence; the top-left diagonal clear gaps contribute
orientation evidence. Candidate acceptance remains cryptographically grounded
in framing/CRC and mathematically grounded in RS, rather than in visual
finder matching alone. Full-frame localization and physical robustness limits
remain implementation work, not format claims.

## Compatibility fixture

`tests/vectors/locator-v11.json` is the byte interoperability fixture for
payload `00 01 ... 0F`. The current renderer and C ABI must produce and decode
it exactly.
