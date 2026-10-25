#!/usr/bin/env python3
"""Build a German TRIVIA pack without replacing or modifying the English pack.

Input is the original curated pack.jsonl plus human-reviewed translations.
Neither English nor German strings belong in firmware translation headers:
both question sets are stored as XTRIVIA v1 files on the SD card.
"""
import argparse
import hashlib
import json
from pathlib import Path

import pack_format
import textfit

MAX_RECORD = 448
CLUE_WIDTH, CLUE_HEIGHT = 448, 583
CLUE_FONT = "reading_serif_14"
FIELDS = ("q", "a", "alt", "w")


class TranslationError(ValueError):
    pass


def source_hash(item):
    # The stable ID alone cannot detect corrected answers or re-generated
    # distractors. Pin the translation to the exact source question AND options.
    payload = {key: item.get(key, [] if key in ("alt", "w") else "") for key in FIELDS}
    raw = json.dumps(payload, ensure_ascii=False, sort_keys=True,
                     separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def load_jsonl(path):
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if line.strip():
                try:
                    yield json.loads(line)
                except json.JSONDecodeError as e:
                    raise TranslationError(f"{path}:{n}: {e}") from e


def validate_record(original, localized, allow_draft=False):
    ident = original["id"]
    if localized.get("source_hash") != source_hash(original):
        raise TranslationError(f"{ident}: source changed after translation")
    if not (allow_draft or localized.get("reviewed") is True):
        raise TranslationError(f"{ident}: unreviewed translation; refusing release pack")
    for key in FIELDS:
        before = original.get(key, [] if key in ("alt", "w") else "")
        value = localized.get(key)
        if isinstance(before, list):
            if not isinstance(value, list) or len(value) != len(before):
                raise TranslationError(f"{ident}: {key} count/order differs from source")
            fields = value
        else:
            if not isinstance(value, str):
                raise TranslationError(f"{ident}: invalid translated {key}")
            fields = [value]
        if any(not isinstance(x, str) or not x.strip() for x in fields):
            raise TranslationError(f"{ident}: empty or non-text {key}")
    answer = localized["a"].strip().casefold()
    wrong = [s.strip().casefold() for s in localized["w"]]
    if answer in wrong or len(set(wrong)) != len(wrong):
        raise TranslationError(f"{ident}: duplicate or correct distractor")
    if any(a.strip().casefold() in wrong for a in localized["alt"]):
        raise TranslationError(f"{ident}: alternative answer is also a distractor")
    if not textfit.fits(localized["q"], CLUE_WIDTH, CLUE_HEIGHT, CLUE_FONT):
        raise TranslationError(f"{ident}: German question does not fit display")

    # textfit returns width zero for unsupported glyphs; never mistake such a
    # measurement for 'fits'. Firmware uses the same real font metrics.
    glyphs, _ = textfit.font(CLUE_FONT)
    for key in FIELDS:
        value = localized[key]
        for field in (value if isinstance(value, list) else [value]):
            unsupported = sorted({c for c in field if ord(c) not in glyphs
                                  and c not in "\\n\\r\\t"})
            if unsupported:
                raise TranslationError(f"{ident}: glyphs missing in display font: {unsupported}")
    result = {key: original[key] for key in ("id", "d", "y") if key in original}
    result.update({key: localized[key] for key in FIELDS})
    size = len(pack_format._rec(result))
    if size > MAX_RECORD:
        raise TranslationError(f"{ident}: German binary record is {size}/{MAX_RECORD} bytes")
    return result


def build(source_path, translation_path, output_path, allow_draft=False):
    originals = list(load_jsonl(source_path))
    translations = {}
    for row in load_jsonl(translation_path):
        ident = row.get("id")
        if not isinstance(ident, str) or ident in translations:
            raise TranslationError(f"missing or duplicated translation ID: {ident!r}")
        translations[ident] = row
    if not originals:
        raise TranslationError("source pack is empty")
    if not allow_draft and Path(output_path).name != "pack-de.dat":
        raise TranslationError("release output must be named pack-de.dat")
    out = []
    seen = set()
    for original in originals:
        ident = original.get("id")
        if not isinstance(ident, str) or ident in seen:
            raise TranslationError(f"missing or duplicate source ID: {ident!r}")
        seen.add(ident)
        row = translations.get(ident)
        if row is None:
            raise TranslationError(f"{ident}: missing German question; no English fallback in a DE pack")
        out.append(validate_record(original, row, allow_draft))
    extra = set(translations) - seen
    if extra:
        raise TranslationError(f"{len(extra)} translations do not belong to this source pack")
    destination = Path(output_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    tmp = destination.with_suffix(destination.suffix + ".part")
    try:
        written = pack_format.write(out, str(tmp))
        tmp.replace(destination)  # no partial valid pack on failure
    finally:
        if tmp.exists():
            tmp.unlink()
    return len(out), written, sum(bool(item.get("w")) for item in out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="Original curated pack.jsonl")
    ap.add_argument("--translations", required=True, help="Reviewed translations JSONL")
    ap.add_argument("--out", required=True, help="Path ending in pack-de.dat")
    ap.add_argument("--allow-draft", action="store_true",
                    help="Local QA only: permit unreviewed entries; DO NOT RELEASE")
    args = ap.parse_args()
    try:
        n, size, solo = build(args.src, args.translations, args.out, args.allow_draft)
    except TranslationError as exc:
        ap.error(str(exc))
    print(f"German pack: {n} questions, {solo} solo questions, {size} bytes -> {args.out}")


if __name__ == "__main__":
    main()
