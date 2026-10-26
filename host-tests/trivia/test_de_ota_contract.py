#!/usr/bin/env python3
"""Avoid regression: OTA image vs language-separated SD question downloads."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
activity=(root/"src/apps_local/trivia/TriviaActivity.cpp").read_text()
screen=(root/"src/apps_local/trivia/TriviaScreens.cpp").read_text()
ota=(root/"src/network/OtaUpdater.cpp").read_text()
release=(root/".github/workflows/crossplay-release.yml").read_text()
assert "releases/latest" in ota
assert "trivia-de-pack/pack-de.dat" in activity
assert '/trivia/pack-de.dat.part' in activity
assert 'packUrl = downloadingGerman ? kGermanPackUrl : kPackUrl' in activity
assert 'packPath =' in activity and 'kGermanPackLocation.packPath' in activity
assert 'candidatePack.flags() == requiredPackFlag' in activity
assert 'Storage.rename(partPath, packPath)' in activity
assert 'screen.frame().hit(downloadBox, ActionGetPack)' in screen
assert 'GET GERMAN QUESTIONS' in screen
assert "### What is new in 1.16.12" in release
print("German trivia SD download and OTA separation checks OK")
