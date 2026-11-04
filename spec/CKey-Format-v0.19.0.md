# CKey Format v0.19.0 — Profile 2 Frame Safety Moat

Profile 2 retains its 29-unit field, RS(255,210) frame, deterministic visual
fill, and Grid/Ring/Ray calibration keys. Its payload lattice now reserves a
1.35-unit clear moat around the continuous border. A module that lands in the
frame's raster or blur fringe is not an optical data site.

The change is geometric and changes the codeword-to-site mapping. Encoders
emit frame format byte `0x13` (decimal 19), profile byte `0x02`. A Profile 2
reader must use this site's mapping for format 19; earlier format mappings are
historical decode paths only.
