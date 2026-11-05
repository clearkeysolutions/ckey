#!/usr/bin/env python3
"""Bump the build number in every versioned node (CONTRIBUTING_VERSIONING.md).

    tools/bump_version.py            # 0.1.0-build.N -> 0.1.0-build.N+1
    tools/bump_version.py --check    # list the files that carry the version

The changelog entry is still written by hand; tools/check_version.py fails
until its top heading matches.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
FILES = [
    "VERSION", "CONTEXT.md", "include/ckey/ckey.h", "tests/vectors/locator-v14.json",
    "ios/README.md", "macos/README.md", "docs/support-matrix.json",
    "sdk/web/package.json", "sdk/android/gradle.properties", "sdk/dotnet/Directory.Build.props",
    "sdk/swift/Sources/CKey/CKeyVersion.swift",
]
old = (root / "VERSION").read_text().strip()
m = re.fullmatch(r"(\d+\.\d+\.\d+)-build\.(\d+)", old)
if not m:
    sys.exit(f"VERSION {old!r} is not of the form X.Y.Z-build.N")
present = [f for f in FILES if (root / f).exists() and old in (root / f).read_text()]
if "--check" in sys.argv:
    print("\n".join(present)); sys.exit(0)
new = f"{m.group(1)}-build.{int(m.group(2)) + 1}"
for f in present:
    p = root / f
    p.write_text(p.read_text().replace(old, new))
print(f"{old} -> {new} in {len(present)} files:\n  " + "\n  ".join(present))
