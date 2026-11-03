#!/usr/bin/env python3
"""Nested corner finders: move center to tip, set radius = border rx."""
from pathlib import Path
import sys

path = Path("src/ckey.cpp")
if not path.exists():
    sys.exit("Run from the ckey repo root")

src = path.read_text()
if "cx = 0.8, cy = 0.8, radius = 3.5" in src or "cx=0.8, cy=0.8, radius=3.5" in src:
    print("Already applied.")
    sys.exit(0)

n = 0

# Profile 1
a = "constexpr double cx = 4.0, cy = 4.0, radius = 3.15, thickness = 1.05, visible_gap = 1.0;"
b = "constexpr double cx = 0.8, cy = 0.8, radius = 3.5, thickness = 1.0, visible_gap = 1.1;"
if a in src:
    src = src.replace(a, b, 1)
    n += 1
    print("Updated profile 1 constants")
else:
    print("WARN: profile 1 constants not found")

# Profile 2
a = "constexpr double cx=4.0, cy=4.0, radius=3.15, thickness=1.05;"
b = "constexpr double cx=0.8, cy=0.8, radius=3.5, thickness=1.0;"
if a in src:
    src = src.replace(a, b, 1)
    n += 1
    print("Updated profile 2 constants")
else:
    print("WARN: profile 2 constants not found")

src2 = src.replace("A 3.1500 3.1500", "A 3.5000 3.5000")
src2 = src2.replace('stroke-width="1.0500"', 'stroke-width="1.0000"')
if src2 != src:
    src = src2
    print("Updated profile 2 path radii/stroke")

# Profile 3
a = "constexpr double cx=16.0,cy=16.0,r=12.6,thickness=4.2;"
b = "constexpr double cx=3.2,cy=3.2,r=14.0,thickness=4.0;"
if a in src:
    src = src.replace(a, b, 1)
    n += 1
    print("Updated profile 3 constants")
else:
    print("WARN: profile 3 constants not found")

src2 = src.replace("A 12.6000 12.6000", "A 14.0000 14.0000")
src2 = src2.replace('stroke-width="4.2000"', 'stroke-width="4.0000"')
if src2 != src:
    src = src2
    print("Updated profile 3 path radii/stroke")

if n < 2:
    print("FAILED: too few replacements. Run: git checkout -- src/ckey.cpp")
    sys.exit(1)

path.write_text(src)
print("Wrote", path)
print("nested tip center present:", "0.8, cy = 0.8, radius = 3.5" in path.read_text() or "0.8, cy=0.8, radius=3.5" in path.read_text())
