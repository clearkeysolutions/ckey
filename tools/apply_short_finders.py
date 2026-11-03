#!/usr/bin/env python3
"""Apply short corner-accent finder geometry to src/ckey.cpp"""
from pathlib import Path
import re
import sys

path = Path("src/ckey.cpp")
if not path.exists():
    print("Run from the ckey repo root (where src/ckey.cpp lives).")
    sys.exit(1)

src = path.read_text()

if "half_span" in src and "only the corner bend" in src:
    print("Already has short-arc finders.")
    sys.exit(0)

p1_new = r'''    // Short corner accents on the outer border curve (same center + radius as
    // the rounded-rect rx). Not full quarter-circles — only the corner bend.
    // Top-left is split for orientation.
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? "" : corner == 1 ? "translate(29 0) scale(-1 1)" : corner == 2 ? "translate(29 29) scale(-1 -1)" : "translate(0 29) scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        // Border rx=3.5 on the 0.5..28.5 rect → corner center (4.0, 4.0).
        constexpr double cx = 4.0, cy = 4.0, radius = 3.5, thickness = 0.85, visible_gap = 0.9;
        // ±28° from the 45° diagonal → ~56° arc (corner accent, not a full C).
        const double middle = kTau * 45.0 / 360.0;
        const double half_span = kTau * 28.0 / 360.0;
        if (corner == 0) {
            const double half_gap = (visible_gap + thickness) / (2.0 * radius);
            const double outer1 = middle + half_span;
            const double inner1 = middle + half_gap;
            const double inner2 = middle - half_gap;
            const double outer2 = middle - half_span;
            o << "<path d=\"M " << cx + radius * std::cos(outer1) << ' ' << cy + radius * std::sin(outer1)
              << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius * std::cos(inner1) << ' ' << cy + radius * std::sin(inner1)
              << " M " << cx + radius * std::cos(inner2) << ' ' << cy + radius * std::sin(inner2)
              << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius * std::cos(outer2) << ' ' << cy + radius * std::sin(outer2)
              << "\" stroke-width=\"" << thickness << "\"/>";
        } else {
            const double a1 = middle + half_span;
            const double a2 = middle - half_span;
            o << "<path d=\"M " << cx + radius * std::cos(a1) << ' ' << cy + radius * std::sin(a1)
              << " A " << radius << ' ' << radius << " 0 0 0 " << cx + radius * std::cos(a2) << ' ' << cy + radius * std::sin(a2)
              << "\" stroke-width=\"" << thickness << "\"/>";
        }
        o << "</g>";
    }
'''

# Profile 1
anchor1 = 'role="img" aria-label="CKey locator symbol">'
idx1 = src.find(anchor1)
if idx1 < 0:
    print("Could not find locator SVG anchor")
    sys.exit(1)

for_start = src.find('for (int corner = 0; corner < 4; ++corner)', idx1)
if for_start < 0:
    print("Could not find locator corner loop")
    sys.exit(1)

close_pat = '        o << "</g>";\n    }'
close_idx = src.find(close_pat, for_start)
if close_idx < 0:
    print("Could not find end of locator corner loop")
    sys.exit(1)
close_end = close_idx + len(close_pat)

block_start = for_start
line_start = src.rfind('\n', 0, for_start) + 1
while line_start > 0:
    prev_nl = src.rfind('\n', 0, line_start - 1)
    line = src[prev_nl+1:line_start]
    if line.strip().startswith('//') or line.strip() == '':
        line_start = prev_nl + 1
    else:
        break
block_start = line_start

src = src[:block_start] + p1_new + src[close_end:]
print("Replaced profile 1 finder loop")

# Profile 2
anchor2 = 'aria-label="CKey text symbol profile 2">'
idx2 = src.find(anchor2)
if idx2 < 0:
    print("WARN: no profile 2 anchor")
else:
    for_start = src.find('for (int corner = 0; corner < 4; ++corner)', idx2)
    close_idx = src.find(close_pat, for_start)
    close_end = close_idx + len(close_pat)
    block_start = for_start
    line_start = src.rfind('\n', 0, for_start) + 1
    while line_start > 0:
        prev_nl = src.rfind('\n', 0, line_start - 1)
        line = src[prev_nl+1:line_start]
        if line.strip().startswith('//') or line.strip() == '':
            line_start = prev_nl + 1
        else:
            break
    block_start = line_start
    p2_new = r'''    // Short corner accents (same geometry as profile 1 finders).
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? "" : corner == 1 ? "translate(29 0) scale(-1 1)" : corner == 2 ? "translate(29 29) scale(-1 -1)" : "translate(0 29) scale(1 -1)";
        o << "<g transform=\"" << transform << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double cx=4.0, cy=4.0, radius=3.5, thickness=0.85;
        const double middle = kTau / 8.0, half_span = kTau * 28.0 / 360.0;
        if (corner == 0) {
            const double gap = (0.9 + thickness) / (2.0 * radius);
            o << "<path d=\"M " << cx + radius * std::cos(middle + half_span) << ' ' << cy + radius * std::sin(middle + half_span)
              << " A 3.5000 3.5000 0 0 0 " << cx + radius * std::cos(middle + gap) << ' ' << cy + radius * std::sin(middle + gap)
              << " M " << cx + radius * std::cos(middle - gap) << ' ' << cy + radius * std::sin(middle - gap)
              << " A 3.5000 3.5000 0 0 0 " << cx + radius * std::cos(middle - half_span) << ' ' << cy + radius * std::sin(middle - half_span)
              << "\" stroke-width=\"0.8500\"/>";
        } else o << "<path d=\"M " << cx + radius * std::cos(middle + half_span) << ' ' << cy + radius * std::sin(middle + half_span)
              << " A 3.5000 3.5000 0 0 0 " << cx + radius * std::cos(middle - half_span) << ' ' << cy + radius * std::sin(middle - half_span)
              << "\" stroke-width=\"0.8500\"/>";
        o << "</g>";
    }
'''
    src = src[:block_start] + p2_new + src[close_end:]
    print("Replaced profile 2 finder loop")

# Profile 3
anchor3 = 'aria-label="CKey extended symbol profile 3">'
idx3 = src.find(anchor3)
if idx3 < 0:
    print("WARN: no profile 3 anchor")
else:
    for_start = src.find('for (int corner=0;corner<4;++corner)', idx3)
    if for_start < 0:
        for_start = src.find('for (int corner = 0; corner < 4; ++corner)', idx3)
    close_idx = src.find('        o << "</g>";\n    }', for_start)
    if close_idx < 0:
        close_idx = src.find('o << "</g>";\n    }', for_start)
        close_end = close_idx + len('o << "</g>";\n    }')
    else:
        close_end = close_idx + len('        o << "</g>";\n    }')
    block_start = for_start
    line_start = src.rfind('\n', 0, for_start) + 1
    while line_start > 0:
        prev_nl = src.rfind('\n', 0, line_start - 1)
        line = src[prev_nl+1:line_start]
        if line.strip().startswith('//') or line.strip() == '':
            line_start = prev_nl + 1
        else:
            break
    block_start = line_start
    p3_new = r'''    // Short corner accents on the outer border curve (rx=14 → center 16,16).
    for (int corner=0;corner<4;++corner) {
        const char *t=corner==0?"":corner==1?"translate(116 0) scale(-1 1)":corner==2?"translate(116 116) scale(-1 -1)":"translate(0 116) scale(1 -1)";
        o << "<g transform=\"" << t << "\" fill=\"none\" stroke-linecap=\"round\">";
        constexpr double cx=16.0,cy=16.0,r=14.0,thickness=3.4;
        const double middle=kTau/8.0, half_span=kTau*28.0/360.0;
        if (corner==0) {
            const double gap=(3.6+thickness)/(2.0*r);
            o << "<path d=\"M " << cx+r*std::cos(middle+half_span) << ' ' << cy+r*std::sin(middle+half_span)
              << " A 14.0000 14.0000 0 0 0 " << cx+r*std::cos(middle+gap) << ' ' << cy+r*std::sin(middle+gap)
              << " M " << cx+r*std::cos(middle-gap) << ' ' << cy+r*std::sin(middle-gap)
              << " A 14.0000 14.0000 0 0 0 " << cx+r*std::cos(middle-half_span) << ' ' << cy+r*std::sin(middle-half_span)
              << "\" stroke-width=\"3.4000\"/>";
        } else o << "<path d=\"M " << cx+r*std::cos(middle+half_span) << ' ' << cy+r*std::sin(middle+half_span)
              << " A 14.0000 14.0000 0 0 0 " << cx+r*std::cos(middle-half_span) << ' ' << cy+r*std::sin(middle-half_span)
              << "\" stroke-width=\"3.4000\"/>";
        o << "</g>";
    }
'''
    src = src[:block_start] + p3_new + src[close_end:]
    print("Replaced profile 3 finder loop")

path.write_text(src)
print("Wrote", path)
print("half_span present:", "half_span" in path.read_text())
print("radius = 3.5 present:", "radius = 3.5" in path.read_text())
