#!/usr/bin/env python3
"""Own CAW decks kept outside git (e.g. packs.local/) as a publish source.

A source is the deck's manifest (deck.json); its cards are deck.jsonl next to
it, in the build_sd_pack.py row format. Card text stays out of the
repository, exactly like the cloned community sources: publish_packs.py only
pins the resulting pack's SHA-256.

    python3 publish_packs.py --config public_packs.json \\
        --source nach-mitternacht=../../packs.local/nach-mitternacht.json ...
"""
from __future__ import annotations
import json
from pathlib import Path

from card_text import BLANK, normalized, tidy


class ImportError_(ValueError):
    pass


def _paths(source):
    manifest = Path(source)
    if manifest.suffix != ".json":
        raise ImportError_(f"{manifest} is not a deck manifest (.json)")
    return manifest, manifest.with_suffix(".jsonl")


def _load_manifest(source):
    path, _ = _paths(source)
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("deck_id"), str):
        raise ImportError_(f"{path} has no deck_id")
    return data


def convert(source):
    """Rows for merge_cards, issues (bad cards), duplicates dropped."""
    manifest = _load_manifest(source)
    known = {c["id"] for c in manifest.get("categories", [])}
    _, cards_path = _paths(source)
    rows, issues, duplicates, seen = [], [], [], {}
    for n, line in enumerate(cards_path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        where = f"{cards_path.name}:{n}"
        try:
            card = json.loads(line)
            kind, text = card.get("kind"), card.get("text")
            if kind not in ("prompt", "answer") or not isinstance(text, str) or not text:
                raise ImportError_("kind or text missing")
            if tidy(text) != text:
                raise ImportError_("text not normalized (blanks, spaces, glyphs)")
            cats = card.get("categories", [])
            if not isinstance(cats, list) or not set(cats) <= known:
                raise ImportError_("unknown category")
            row = {"id": card.get("id"), "kind": kind, "text": text, "categories": list(cats),
                   "source_line": where}
            if kind == "prompt":
                pick, draw = card.get("pick", 1), card.get("draw", 0)
                if pick not in (1, 2, 3) or draw not in (0, 1, 2) or text.count(BLANK) not in (0, pick):
                    raise ImportError_("pick, draw and blanks disagree")
                row.update(pick=pick, draw=draw)
            elif BLANK in text:
                raise ImportError_("answer contains a blank")
        except (ImportError_, json.JSONDecodeError, AttributeError) as exc:
            issues.append({"where": where, "problem": str(exc), "text": line[:120]})
            continue
        key = (kind, normalized(text))
        if key in seen:
            duplicates.append({"kept": seen[key], "dropped": where, "text": text})
            continue
        seen[key] = where
        rows.append(row)
    return rows, issues, duplicates


def manifest_for(source, commit):
    """Importer manifest in the shape merge_cards expects."""
    data = _load_manifest(source)
    categories = [{"id": c["id"], "name": c["name"], "default_on": bool(c.get("default_on", True)),
                   "adult": bool(c.get("adult", False))} for c in data.get("categories", [])]
    return {
        "deck_id": data["deck_id"], "version": data.get("version", 1), "source_locale": "de",
        "title": data.get("title", data["deck_id"]),
        "source_url": data.get("source_url"), "source_commit": commit,
        "rights_holder": data.get("rights_holder"), "license": data.get("license"),
        "license_url": data.get("license_url"),
        "rights_status": "unverified", "editorial_status": "unreviewed",
        "categories": categories,
    }
