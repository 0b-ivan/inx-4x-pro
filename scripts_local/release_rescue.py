#!/usr/bin/env python3
from pathlib import Path
import subprocess

EXPECTED_FIX = "c6f57d13facbcd9163e12b375fee28e52a9c1a90"

subprocess.run(["git", "merge-base", "--is-ancestor", EXPECTED_FIX, "HEAD"], check=True)

ini = Path("platformio.ini")
text = ini.read_text()
old = "[crossplay]\nversion = 1.12.17"
new = "[crossplay]\nversion = 1.12.18"
if text.count(old) != 1:
    raise SystemExit("expected exactly one crossplay 1.12.17 version")
ini.write_text(text.replace(old, new, 1))

release_wf = Path(".github/workflows/crossplay-release.yml").read_text()
if "### What is new in 1.12.18" not in release_wf:
    raise SystemExit("v1.12.18 release notes are not prepared")
