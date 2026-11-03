#!/usr/bin/env python3
"""Rewrite finder arcs to short corner accents (~56° on border radius)."""
from pathlib import Path
import sys

path = Path("src/ckey.cpp")
if not path.exists():
    sys.exit("Run from the ckey repo root (folder that contains src/ckey.cpp)")

src = path.read_text()
if "half_span" in src and "only the corner bend" in src:
    print("Already applied.")
    sys.exit(0)

# ---- Profile 1 (exact block from main) ----
old1 = """    // Finder arcs share the outer border's corner radius centers so they
    // follow the rounded-square frame (same curvature as the boundary).
    // Top-left remains split for orientation.
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? \"\" : corner == 1 ? \"translate(29 0) scale(-1 1)\" : corner == 2 ? \"translate(29 29) scale(-1 -1)\" : \"translate(0 29) scale(1 -1)\";
        o << \"<g transform=\\\"\" << transform << \"\\\" fill=\\\"none\\\" stroke-linecap=\\\"round\\\">\";
        // Border is rx=3.5 on the 0.5..28.5 rect → corner center at (4.0, 4.0).
        // Finder radius sits just inside the border stroke so both remain visible.
        constexpr double cx = 4.0, cy = 4.0, radius = 3.15, thickness = 1.05, visible_gap = 1.0;
        if (corner == 0) {
            const double half_gap = (visible_gap + thickness) / (2.0 * radius);
            const double middle = kTau * 45.0 / 360.0;
            const double a1 = middle + half_gap;
            const double a2 = middle - half_gap;
            o << \"<path d=\\\"M \" << cx << ' ' << cy + radius << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius * std::cos(a1) << ' ' << cy + radius * std::sin(a1)
              << \" M \" << cx + radius * std::cos(a2) << ' ' << cy + radius * std::sin(a2) << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius << ' ' << cy
              << \"\\\" stroke-width=\\\"\" << thickness << \"\\\"/>\";
        } else {
            o << \"<path d=\\\"M \" << cx << ' ' << cy + radius << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius << ' ' << cy
              << \"\\\" stroke-width=\\\"\" << thickness << \"\\\"/>\";
        }
        o << \"</g>\";
    }"""

new1 = """    // Short corner accents on the outer border curve (same center + radius as
    // the rounded-rect rx). Not full quarter-circles — only the corner bend.
    // Top-left is split for orientation.
    for (int corner = 0; corner < 4; ++corner) {
        const char *transform = corner == 0 ? \"\" : corner == 1 ? \"translate(29 0) scale(-1 1)\" : corner == 2 ? \"translate(29 29) scale(-1 -1)\" : \"translate(0 29) scale(1 -1)\";
        o << \"<g transform=\\\"\" << transform << \"\\\" fill=\\\"none\\\" stroke-linecap=\\\"round\\\">\";
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
            o << \"<path d=\\\"M \" << cx + radius * std::cos(outer1) << ' ' << cy + radius * std::sin(outer1)
              << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius * std::cos(inner1) << ' ' << cy + radius * std::sin(inner1)
              << \" M \" << cx + radius * std::cos(inner2) << ' ' << cy + radius * std::sin(inner2)
              << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius * std::cos(outer2) << ' ' << cy + radius * std::sin(outer2)
              << \"\\\" stroke-width=\\\"\" << thickness << \"\\\"/>\";
        } else {
            const double a1 = middle + half_span;
            const double a2 = middle - half_span;
            o << \"<path d=\\\"M \" << cx + radius * std::cos(a1) << ' ' << cy + radius * std::sin(a1)
              << \" A \" << radius << ' ' << radius << \" 0 0 0 \" << cx + radius * std::cos(a2) << ' ' << cy + radius * std::sin(a2)
              << \"\\\" stroke-width=\\\"\" << thickness << \"\\\"/>\";
        }
        o << \"</g>\";
    }"""

# ---- Profile 2 ----
old2 = """        constexpr double cx=4.0, cy=4.0, radius=3.15, thickness=1.05;
        if (corner == 0) {
            const double gap = (1.0 + thickness) / (2.0 * radius), middle = kTau / 8.0;
            o << \"<path d=\\\"M \" << cx << ' ' << cy + radius << \" A 3.1500 3.1500 0 0 0 \" << cx + radius * std::cos(middle + gap) << ' ' << cy + radius * std::sin(middle + gap)
              << \" M \" << cx + radius * std::cos(middle - gap) << ' ' << cy + radius * std::sin(middle - gap) << \" A 3.1500 3.1500 0 0 0 \" << cx + radius << ' ' << cy << \"\\\" stroke-width=\\\"1.0500\\\"/>\";
        } else o << \"<path d=\\\"M \" << cx << ' ' << cy + radius << \" A 3.1500 3.1500 0 0 0 \" << cx + radius << ' ' << cy << \"\\\" stroke-width=\\\"1.0500\\\"/>\";"""

new2 = """        constexpr double cx=4.0, cy=4.0, radius=3.5, thickness=0.85;
        const double middle = kTau / 8.0, half_span = kTau * 28.0 / 360.0;
        if (corner == 0) {
            const double gap = (0.9 + thickness) / (2.0 * radius);
            o << \"<path d=\\\"M \" << cx + radius * std::cos(middle + half_span) << ' ' << cy + radius * std::sin(middle + half_span)
              << \" A 3.5000 3.5000 0 0 0 \" << cx + radius * std::cos(middle + gap) << ' ' << cy + radius * std::sin(middle + gap)
              << \" M \" << cx + radius * std::cos(middle - gap) << ' ' << cy + radius * std::sin(middle - gap)
              << \" A 3.5000 3.5000 0 0 0 \" << cx + radius * std::cos(middle - half_span) << ' ' << cy + radius * std::sin(middle - half_span)
              << \"\\\" stroke-width=\\\"0.8500\\\"/>\";
        } else o << \"<path d=\\\"M \" << cx + radius * std::cos(middle + half_span) << ' ' << cy + radius * std::sin(middle + half_span)
              << \" A 3.5000 3.5000 0 0 0 \" << cx + radius * std::cos(middle - half_span) << ' ' << cy + radius * std::sin(middle - half_span)
              << \"\\\" stroke-width=\\\"0.8500\\\"/>\";"""

# ---- Profile 3 ----
old3 = """        constexpr double cx=16.0,cy=16.0,r=12.6,thickness=4.2;
        if (corner==0) {
            const double gap=(4.0+thickness)/(2.0*r), middle=kTau/8.0;
            o << \"<path d=\\\"M \" << cx << ' ' << cy+r << \" A 12.6000 12.6000 0 0 0 \" << cx+r*std::cos(middle+gap) << ' ' << cy+r*std::sin(middle+gap)
              << \" M \" << cx+r*std::cos(middle-gap) << ' ' << cy+r*std::sin(middle-gap) << \" A 12.6000 12.6000 0 0 0 \" << cx+r << ' ' << cy << \"\\\" stroke-width=\\\"4.2000\\\"/>\";
        } else o << \"<path d=\\\"M \" << cx << ' ' << cy+r << \" A 12.6000 12.6000 0 0 0 \" << cx+r << ' ' << cy << \"\\\" stroke-width=\\\"4.2000\\\"/>\";"""

new3 = """        constexpr double cx=16.0,cy=16.0,r=14.0,thickness=3.4;
        const double middle=kTau/8.0, half_span=kTau*28.0/360.0;
        if (corner==0) {
            const double gap=(3.6+thickness)/(2.0*r);
            o << \"<path d=\\\"M \" << cx+r*std::cos(middle+half_span) << ' ' << cy+r*std::sin(middle+half_span)
              << \" A 14.0000 14.0000 0 0 0 \" << cx+r*std::cos(middle+gap) << ' ' << cy+r*std::sin(middle+gap)
              << \" M \" << cx+r*std::cos(middle-gap) << ' ' << cy+r*std::sin(middle-gap)
              << \" A 14.0000 14.0000 0 0 0 \" << cx+r*std::cos(middle-half_span) << ' ' << cy+r*std::sin(middle-half_span)
              << \"\\\" stroke-width=\\\"3.4000\\\"/>\";
        } else o << \"<path d=\\\"M \" << cx+r*std::cos(middle+half_span) << ' ' << cy+r*std::sin(middle+half_span)
              << \" A 14.0000 14.0000 0 0 0 \" << cx+r*std::cos(middle-half_span) << ' ' << cy+r*std::sin(middle-half_span)
              << \"\\\" stroke-width=\\\"3.4000\\\"/>\";"""

ok = True
if old1 not in src:
    print("ERROR: profile 1 block not found (file may already be partially edited)")
    ok = False
else:
    src = src.replace(old1, new1, 1)
    print("Replaced profile 1")

if old2 not in src:
    print("ERROR: profile 2 block not found")
    ok = False
else:
    src = src.replace(old2, new2, 1)
    print("Replaced profile 2")

if old3 not in src:
    print("ERROR: profile 3 block not found")
    ok = False
else:
    src = src.replace(old3, new3, 1)
    print("Replaced profile 3")

if not ok:
    sys.exit(1)

path.write_text(src)
print("Wrote", path)
print("half_span:", "half_span" in path.read_text())
