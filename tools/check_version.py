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
fixture = json.loads((root / "tests/vectors/locator-v7.json").read_text())
format_doc = root / "spec/CKey-Format-v0.7.0.md"
format_text = format_doc.read_text()

software_checks = {
    "public C ABI header": re.search(r'#define CKEY_SOFTWARE_VERSION "([^"]+)"', header).group(1),
    "living context": re.search(r"Current software version:\*\* `([^`]+)`", context).group(1),
    "interoperability fixture": fixture["software_version"],
    "changelog head": re.search(r"^## ([^ ]+)", changelog, re.MULTILINE).group(1),
}
stale = {name: value for name, value in software_checks.items() if value != version}
format_checks = {
    "format specification": "format=7, profile=1" in format_text,
    "public format byte": re.search(r"#define CKEY_FORMAT_VERSION (\d+)u", header).group(1) == "7",
    "living context format": "CKey Format 0.7.0" in context and "format byte `07`" in context,
    "fixture format": fixture["format_version"] == "0.7.0",
    "current README link": "spec/CKey-Format-v0.7.0.md" in (root / "README.md").read_text(),
}
if not all(format_checks.values()):
    raise SystemExit(f"Current format nodes are inconsistent: {format_checks}")
if stale:
    raise SystemExit(f"VERSION is {version}; stale version references: {stale}")
print(f"Software and format version nodes are synchronized: {version} / 0.7.0")
