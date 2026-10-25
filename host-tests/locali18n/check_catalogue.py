#!/usr/bin/env python3
"""Assert centralized app/Tarot translations and generated files stay in sync."""
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import (  # noqa: E402
    generate_local_app_strings,
    generate_tarot_strings,
    generate_local_formats,
    parse_yaml_file,
)

translations = ROOT / "lib/I18n/translations"
english = parse_yaml_file(str(translations / "english.yaml"))
german = parse_yaml_file(str(translations / "german.yaml"))

core = [key for key in english if key.startswith("STR_")]
local = [key for key in english if key.startswith("LOCAL_") and not key.startswith(("LOCAL_TAROT_", "LOCAL_FORMAT_"))]
formats = [key for key in english if key.startswith("LOCAL_FORMAT_")]
assert len(core) >= 541, f"Core reference keys decreased unexpectedly: {len(core)}"
assert all(key in german and german[key].strip() for key in core), "German core translation missing"
assert len(local) >= 1200, f"Local app keys decreased unexpectedly: {len(local)}"
assert len(formats) >= 80 and all(key in german for key in formats), "Missing German dynamic status formats"
assert all(key in german and german[key].strip() for key in local), "German app translation missing"

seen = {}
for key in local:
    text = english[key]
    assert text and text not in seen, f"Duplicate English lookup text: {seen.get(text)} / {key}"
    seen[text] = key

for i in range(78):
    for kind in ("NAME", "MEANING"):
        key = f"LOCAL_TAROT_{kind}_{i:02d}"
        assert english.get(key) and german.get(key), f"Missing card translation: {key}"

assert not any(key.startswith("LOCAL_") for key in core)
assert not any(key.startswith("LOCAL_") and key not in english for key in german)

with tempfile.TemporaryDirectory() as temp:
    local_generated = Path(temp) / "LocalI18nData.inc"
    tarot_generated = Path(temp) / "TarotI18nData.h"
    formats_generated = Path(temp) / "LocalI18nFormats.h"
    generate_local_app_strings(str(translations), str(local_generated))
    generate_tarot_strings(str(translations), str(tarot_generated))
    generate_local_formats(str(translations), str(formats_generated))
    for generated, checked_in in (
        (local_generated, ROOT / "src/apps_local/ui/LocalI18nData.inc"),
        (tarot_generated, ROOT / "src/apps_local/tarot/TarotI18nData.h"),
        (formats_generated, ROOT / "src/apps_local/ui/LocalI18nFormats.h"),
    ):
        assert generated.read_bytes() == checked_in.read_bytes(), (
            f"Generated catalogue differs: {checked_in}. "
            "Run python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/"
        )

print(f"locali18n catalogue: {len(core)} core, {len(local)} local labels, {len(formats)} dynamic formats, 78 tarot cards; reproducible")
