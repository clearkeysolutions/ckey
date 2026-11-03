# CKey locator format v0.13.0 (draft)

Status: experimental. New locator frames use `format=13` (`0D`) and
`profile=1`; older locator formats remain decoder-readable for compatibility.

## External-corner finder geometry

The canonical field is 29 by 29 units. Its finder geometry forms the **actual
convex outside corners of the symbol**, not marks inset inside a rounded
rectangle. The four straight frame sides run at coordinate `0.5` / `28.5` and
stop at the corner tangent points. Each finder is three round-capped quarter
arcs with common centre at the physical corner: `(0.5,0.5)` for top-left and
its reflections for the other corners. Centerline radii are 1.80, 2.65, and
3.50 units; all strokes are 0.65 units. The 3.50-unit arc joins the structural
side segments and is therefore the rounded outer corner itself.

Top-right, bottom-right, and bottom-left arc sets are continuous. Every
top-left arc has a cap-compensated 0.95-unit visible gap centered on the
45-degree diagonal. That split is the orientation signature. The corner
envelopes remain reserved from payload.

## Remaining locator rules

All other Format 0.8/0.12 locator rules remain: 16-byte payload; frame
`C7 4B 0D 01 00 10`, CRC-32C, systematic shortened RS(77,26) over GF(256)
with polynomial `0x11D`, 51 parity bytes, and up to 25 unknown byte-error
correction. Grid Key, Ray Key, Ring Key, Cartesian/polar placement, rings,
ray band, and logo-safe centre are unchanged. The reader uses keys and finder
geometry for optical fit but only accepts a result after RS and CRC validation.

The normative vector is [`locator-v13.json`](../tests/vectors/locator-v13.json).
