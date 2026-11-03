## 0.1.0-dev.32 — 2026-11-03

### Changed
- Finder arcs on all three profiles now use the same corner centers and curvature family as the outer rounded-square border (Profile 1/2: center 4.0 radius 3.15; Profile 3: center 16.0 radius 12.6). This replaces the previous inward "sea-shape" arcs centered near the corner tips.
- Top-left orientation gap is retained.
- Profile 3 Cartesian finder clearance centers updated to the new border-matched centers so reserved interiors stay aligned with the drawn strokes.
- Format version bytes: locator `0B` (0.11), text `0C` (0.12), extended `0D` (0.13).

### Notes
- Data-site placement and RS/CRC framing for Profiles 1 and 2 are unchanged; only the drawn finder geometry and Profile 3 clearance centers moved.
- Optical fixtures and any camera captures of pre-0.1.0-dev.32 symbols will not match the new finder geometry.
- Apply `patches/ckey-finder-border-matched.patch` to `src/ckey.cpp` if the core file was not updated in the same commit series.

