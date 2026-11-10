#!/usr/bin/env python3
"""Fail if the current software version is stale in a versioned project node."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text().strip()
header = (root / "include/ckey/ckey.h").read_text()
# CONTEXT.md is the engineering record kept on `scratch`; release branches
# ship without it, so its checks run only where it exists.
context_path = root / "CONTEXT.md"
context = context_path.read_text() if context_path.exists() else None
changelog = (root / "CHANGELOG.md").read_text()
readme = (root / "README.md").read_text()
fixture = json.loads((root / "tests/vectors/locator-v14.json").read_text())
support = json.loads((root / "docs/support-matrix.json").read_text())
format_doc = root / "spec/CKey-Format-v0.14.0.md"
format_text = format_doc.read_text()
text_format_text = (root / "spec/CKey-Format-v0.25.0.md").read_text()
extended_format_text = (root / "spec/CKey-Format-v0.26.0.md").read_text()
scalable_format_text = (root / "spec/CKey-Format-v0.27.0.md").read_text()
print_format_text = (root / "spec/CKey-Format-v0.28.0.md").read_text()

software_checks = {
    "public C ABI header": re.search(r'#define CKEY_SOFTWARE_VERSION "([^"]+)"', header).group(1),
    "interoperability fixture": fixture["software_version"],
    "changelog head": re.search(r"^## ([^ ]+)", changelog, re.MULTILINE).group(1),
    "support matrix": support["current_build"],
}
if context is not None:
    software_checks["living context"] = re.search(r"Current software version:\*\* `([^`]+)`", context).group(1)
stale = {name: value for name, value in software_checks.items() if value != version}
format_checks = {
    "format specification": "format=14" in format_text and "profile=1" in format_text,
    "public format byte": re.search(r"#define CKEY_FORMAT_VERSION (\d+)u", header).group(1) == "14",
    "fixture format": fixture["format_version"] == "0.14.0",
    "current README links": all(f"spec/CKey-Format-v{v}.md" in readme for v in ("0.14.0", "0.25.0", "0.26.0", "0.27.0", "0.28.0", "0.29.0", "0.30.0")),
    "text format specification": "decimal 25" in text_format_text and "profile byte `0x02`" in text_format_text,
    "extended format specification": "decimal 26" in extended_format_text and "profile byte `0x03`" in extended_format_text,
    "public text format byte": re.search(r"#define CKEY_TEXT_FORMAT_VERSION (\d+)u", header).group(1) == "25",
    "public extended format byte": re.search(r"#define CKEY_EXTENDED_FORMAT_VERSION (\d+)u", header).group(1) == "26",
    "scalable format specification": "decimal 27" in scalable_format_text and "profile byte `0x00`" in scalable_format_text,
    "public scalable format byte": re.search(r"#define CKEY_SCALABLE_FORMAT_VERSION (\d+)u", header).group(1) == "27",
    "support matrix ABI": support["abi"] == int(re.search(r"#define CKEY_ABI_VERSION (\d+)u", header).group(1)),
    "release map and branch changelogs": all((root / f).exists() and f in readme for f in ("docs/RELEASE-AND-VERSIONING-MAP.md", "CHANGELOG-STABLE.md", "CHANGELOG-COMMUNITY.md")),
    "print format specification": "decimal 28" in print_format_text and "profile byte `0x00`" in print_format_text,
    "grouped format specifications": "decimal 29" in (root / "spec/CKey-Format-v0.29.0.md").read_text() and "decimal 30" in (root / "spec/CKey-Format-v0.30.0.md").read_text(),
    "public grouped format bytes": re.search(r"#define CKEY_SCALABLE_GROUPED_FORMAT_VERSION (\d+)u", header).group(1) == "29" and re.search(r"#define CKEY_SCALABLE_PRINT_GROUPED_FORMAT_VERSION (\d+)u", header).group(1) == "30",
    "public print format byte": re.search(r"#define CKEY_SCALABLE_PRINT_FORMAT_VERSION (\d+)u", header).group(1) == "28",
}
if context is not None:
    format_checks.update({
        "living context formats": "CKey Format 0.14.0" in context and "0.25.0" in context and "0.26.0" in context and "byte `0E`" in context,
        # The context must not describe an older dense format as current.
        "living context has no stale current-format claim": not re.search(r"current Format 0\.(1\d|2[0-4])\.0", context),
        "living context scalable format": "0.27.0" in context,
        "living context print format": "0.28.0" in context,
    })
if not all(format_checks.values()):
    raise SystemExit(f"Current format nodes are inconsistent: {format_checks}")
if stale:
    raise SystemExit(f"VERSION is {version}; stale version references: {stale}")
print(f"Software and format version nodes are synchronized: {version} / 0.14.0, 0.25.0, 0.26.0, 0.27.0, 0.28.0")
