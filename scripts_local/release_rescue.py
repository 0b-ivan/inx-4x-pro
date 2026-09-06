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

wf = Path(".github/workflows/crossplay-release.yml")
text = wf.read_text()
old_notes = """            ### What is new in 1.12.17

            - X4 Pro wake behavior fixed: standby wake no longer requires a
              long power-button hold.
            - RSS feed management touch input fixed: tapping list rows now
              correctly opens Add Feed and feed field editors.
            - No protocol or installer contract changes in this hotfix.
"""
new_notes = """            ### What is new in 1.12.18

            - Tarot card rendering now uses the same orientation as the standby
              Tarot screen on the X4 Pro.
            - Fixes the unreleased Tarot orientation hotfix from commit
              c6f57d13facbcd9163e12b375fee28e52a9c1a90.
            - No OTA, partition-table or installer contract changes.
"""
if text.count(old_notes) != 1:
    raise SystemExit("expected exactly one 1.12.17 release-note block")
wf.write_text(text.replace(old_notes, new_notes, 1))

Path(".github/workflows/release-rescue.yml").unlink()
Path("scripts_local/release_rescue.py").unlink()
