#!/usr/bin/env python3
"""Offline spelling repair of existing draft translations; NO API calls.

Keep every id/source_hash and every answer-array position. Only a conservative
vetted map of documented model spelling mistakes is applied. Any changed item
is marked unreviewed again; do not treat this as semantic approval.
"""
import argparse
import json
from pathlib import Path

from build_de_pack import TranslationError, load_jsonl
from german_orthography import normalize_orthography


def repair(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve():
        raise TranslationError("Use a different output path to preserve the original draft")
    seen = set()
    changed_rows = 0
    changed_fields = 0
    count = 0
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp = destination.with_suffix(destination.suffix + ".part")
    try:
        with temp.open("w", encoding="utf-8") as out:
            for record in load_jsonl(source):
                ident = record.get("id")
                if not isinstance(ident, str) or ident in seen:
                    raise TranslationError(f"Invalid or duplicate draft id {ident!r}")
                seen.add(ident)
                fixed = dict(record)
                changed = False
                for key in ("q", "a", "alt", "w"):
                    value = record.get(key)
                    if isinstance(value, str):
                        updated = normalize_orthography(value)
                    elif isinstance(value, list) and all(isinstance(v, str) for v in value):
                        updated = [normalize_orthography(v) for v in value]
                    else:
                        raise TranslationError(f"{ident}: missing/invalid {key}")
                    if updated != value:
                        changed_fields += 1
                        changed = True
                    fixed[key] = updated
                if changed:
                    fixed["reviewed"] = False
                    changed_rows += 1
                out.write(json.dumps(fixed, ensure_ascii=False,
                                     separators=(",", ":")) + "\n")
                count += 1
        temp.replace(destination)
    finally:
        if temp.exists():
            temp.unlink()
    return count, changed_rows, changed_fields


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="Existing untranslated or translated draft JSONL")
    ap.add_argument("--out", required=True, help="Corrected output JSONL (different file)")
    args = ap.parse_args()
    try:
        n, rows, fields = repair(args.src, args.out)
    except TranslationError as e:
        ap.error(str(e))
    print(f"{n} records checked, {rows} corrected, {fields} fields updated; review still required.")


if __name__ == "__main__":
    main()
