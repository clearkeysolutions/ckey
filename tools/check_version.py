#!/usr/bin/env python3
"""Fail if the current software version is stale in a versioned project node."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text().strip()
header = (root / "include/ckey/ckey.h").read_text()
context = (root / "CONTEXT.md").read_text()
changelog = (root / "CHANGELOG.md").read_text()
fixture = json.loads((root / "tests/vectors/locator-v2.json").read_text())

checks = {
    "public C ABI header": re.search(r'#define CKEY_SOFTWARE_VERSION "([^"]+)"', header).group(1),
    "living context": re.search(r"Current software version:\*\* `([^`]+)`", context).group(1),
    "interoperability fixture": fixture["software_version"],
    "changelog head": re.search(r"^## ([^ ]+)", changelog, re.MULTILINE).group(1),
}
stale = {name: value for name, value in checks.items() if value != version}
if stale:
    raise SystemExit(f"VERSION is {version}; stale version references: {stale}")
print(f"Software version nodes are synchronized: {version}")
