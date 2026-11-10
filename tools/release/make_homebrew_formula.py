#!/usr/bin/env python3
"""Write the Homebrew formula for a release tag from its source archive.

    tools/release/make_homebrew_formula.py v0.1.0-build.66 ckey-v0.1.0-build.66.tar.gz > ckey.rb
"""
import hashlib
import sys
from pathlib import Path

if len(sys.argv) != 3:
    sys.exit(__doc__)
tag, archive = sys.argv[1], Path(sys.argv[2])
if not tag.startswith("v0.") or "-build." not in tag:
    sys.exit(f"unexpected tag form: {tag}")
template = (Path(__file__).resolve().parents[2] / "packaging/homebrew/ckey.rb.in").read_text()
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
print(template.replace("@TAG@", tag).replace("@VERSION@", tag[1:]).replace("@SHA256@", digest), end="")
