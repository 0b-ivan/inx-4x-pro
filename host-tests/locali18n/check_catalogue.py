#!/usr/bin/env python3
"""Assert centralized app/Tarot translations and generated files stay in sync."""
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import (  # noqa: E402
    generate_local_app_strings,
    generate_tarot_strings,
    generate_local_formats,
    generate_local_context,
    generate_forehead_strings,
    generate_murdle_strings,
    parse_yaml_file,
)

translations = ROOT / "lib/I18n/translations"
english = parse_yaml_file(str(translations / "english.yaml"))
german = parse_yaml_file(str(translations / "german.yaml"))

core = [key for key in english if key.startswith("STR_")]
local = [key for key in english if key.startswith("LOCAL_") and not key.startswith(("LOCAL_TAROT_", "LOCAL_FORMAT_", "LOCAL_CONTEXT_", "LOCAL_FOREHEAD_", "LOCAL_MURDLE_"))]
forehead = [key for key in english if key.startswith("LOCAL_FOREHEAD_")]
murdle_words = [key for key in english if key.startswith("LOCAL_MURDLE_")]
formats = [key for key in english if key.startswith("LOCAL_FORMAT_")]
context = [key for key in english if key.startswith("LOCAL_CONTEXT_")]
assert len(core) >= 541, f"Core reference keys decreased unexpectedly: {len(core)}"
assert all(key in german and german[key].strip() for key in core), "German core translation missing"
assert len(local) >= 1200, f"Local app keys decreased unexpectedly: {len(local)}"
assert len(formats) >= 80 and all(key in german for key in formats), "Missing German dynamic status formats"
assert len(context) == 38 and all(key in german for key in context), "Missing German contextual translations"
assert len(forehead) == 2460 and all(key in german for key in forehead), "Missing indexed German Forehead cards"
assert len(murdle_words) == 104 and all(key in german for key in murdle_words), "Missing indexed German Murdle texts"
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

# Indexed cards must match the unmodified English source list, otherwise
# generated German cards would silently refer to the wrong English prompts.
word_source = (ROOT / "src/apps_local/forehead/ForeheadWords.h").read_text(encoding="utf-8")
word_block = word_source.split("kEntries[kEntryCount] = {", 1)[1].split("};", 1)[0]
english_words = re.findall(r'"([^"]+)"', word_block)
assert len(english_words) == 2460, "English Forehead deck changed"
for index in range(2460):
    key = f"LOCAL_FOREHEAD_{index:04d}"
    assert english.get(key) == english_words[index], f"Forehead English card index shifted: {key}"

# Murdle sentence data are addressed by the current cast array order, so a
# change in MurdleCast.cpp must not quietly attach German words to new items.
cast = (ROOT / "src/apps_local/murdle/MurdleCast.cpp").read_text(encoding="utf-8")
def cast_block(type_name, array_name, count_name):
    pattern = rf"const {type_name} {array_name}\\[{count_name}\\] = \\{{(.*?)\\}};"
    match = re.search(pattern, cast, re.S)
    assert match, f"Missing Murdle English cast array: {array_name}"
    return match.group(1)
triples = r'\\{\\s*"([^"]+)",\\s*"([^"]+)",\\s*"([^"]+)",\\s*(?:true|false)\\s*\\}'
weapons = re.findall(triples, cast_block("WeaponEntry", "kWeapons", "kWeaponCount"))
places = re.findall(triples, cast_block("PlaceEntry", "kPlaces", "kPlaceCount"))
motives = re.findall(r'\\{\\s*"([^"]+)",\\s*"([^"]+)"\\s*\\}',
                    cast_block("MotiveEntry", "kMotives", "kMotiveCount"))
assert (len(weapons), len(places), len(motives)) == (20, 16, 12)
murdle_groups = {
    "WEAPON_PHRASE": [item[1] for item in weapons],
    "WEAPON_NAME": [item[0] for item in weapons],
    "PLACE_PHRASE": [item[1] for item in places],
    "MOTIVE": [item[1] for item in motives],
    "WEAPON_TRAIT": [item[2] for item in weapons],
    "PLACE_TRAIT": [item[2] for item in places],
}
for prefix, values in murdle_groups.items():
    for index, value in enumerate(values):
        key = f"LOCAL_MURDLE_{prefix}_{index:02d}"
        assert english.get(key) == value, f"Murdle English cast index shifted: {key}"

assert not any(key.startswith("LOCAL_") for key in core)
assert not any(key.startswith("LOCAL_") and key not in english for key in german)

with tempfile.TemporaryDirectory() as temp:
    local_generated = Path(temp) / "LocalI18nData.inc"
    tarot_generated = Path(temp) / "TarotI18nData.h"
    formats_generated = Path(temp) / "LocalI18nFormats.h"
    context_generated = Path(temp) / "LocalI18nContext.h"
    forehead_generated = Path(temp) / "ForeheadWordsDe.h"
    murdle_generated = Path(temp) / "MurdleI18nData.inc"
    generate_local_app_strings(str(translations), str(local_generated))
    generate_tarot_strings(str(translations), str(tarot_generated))
    generate_local_formats(str(translations), str(formats_generated))
    generate_local_context(str(translations), str(context_generated))
    generate_forehead_strings(str(translations), str(forehead_generated))
    generate_murdle_strings(str(translations), str(murdle_generated))
    for generated, checked_in in (
        (local_generated, ROOT / "src/apps_local/ui/LocalI18nData.inc"),
        (tarot_generated, ROOT / "src/apps_local/tarot/TarotI18nData.h"),
        (formats_generated, ROOT / "src/apps_local/ui/LocalI18nFormats.h"),
        (context_generated, ROOT / "src/apps_local/ui/LocalI18nContext.h"),
        (forehead_generated, ROOT / "src/apps_local/forehead/ForeheadWordsDe.h"),
        (murdle_generated, ROOT / "src/apps_local/murdle/MurdleI18nData.inc"),
    ):
        assert generated.read_bytes() == checked_in.read_bytes(), (
            f"Generated catalogue differs: {checked_in}. "
            "Run python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/"
        )

print(f"locali18n catalogue: {len(core)} core, {len(local)} local labels, {len(formats)} dynamic formats, {len(context)} context terms, 78 tarot cards, {len(forehead)} Forehead words, {len(murdle_words)} Murdle terms; reproducible")
