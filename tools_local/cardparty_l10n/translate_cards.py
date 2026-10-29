#!/usr/bin/env python3
"""CAW card localization: checkpointed OpenRouter drafts, NEVER auto-approve.

Source: original/licensed UTF-8 JSONL cards plus a rights declaration.
Drafts: ID + SHA-256 source binding + target locale + text + reviewed=false.
No SD .dat binary or firmware content is produced by this tool.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.request

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
ID_RE = re.compile(r"^(prompt|answer)-[a-z0-9][a-z0-9-]{2,63}$")
LICENSES = {"CC0-1.0", "CC-BY-4.0", "MIT"}  # not proof of ownership
PLACEHOLDER = "____"
MAX_TEXT_BYTES = 512
MAX_CARDS = 100000
TRANSLATION_SCHEMA = {
    "type": "json_schema",
    "json_schema": {
        "name": "cardparty_translation",
        "strict": True,
        "schema": {
            "type": "object",
            "properties": {"text": {"type": "string"}},
            "required": ["text"],
            "additionalProperties": False,
        },
    },
}


class CardError(ValueError):
    pass


def read_jsonl(path):
    with open(path, encoding="utf-8") as stream:
        for lineno, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError as error:
                raise CardError(f"{path}:{lineno}: {error}") from error
            if not isinstance(obj, dict):
                raise CardError(f"{path}:{lineno}: expected JSON object")
            yield obj


def validate_text(value, label):
    if not isinstance(value, str) or not value.strip():
        raise CardError(f"{label}: empty/non-string text")
    if len(value.encode("utf-8")) > MAX_TEXT_BYTES:
        raise CardError(f"{label}: exceeds {MAX_TEXT_BYTES} UTF-8 bytes")
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise CardError(f"{label}: contains control characters")
    # HTML client uses textContent, nevertheless reject markup as source data.
    if "<" in value or ">" in value:
        raise CardError(f"{label}: HTML-like text not allowed")


def load_source(manifest_path, source_path):
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or set(manifest) != {
            "deck_id", "version", "source_locale", "license", "rights_holder"}:
        raise CardError("manifest must declare deck_id/version/source_locale/license/rights_holder")
    if not re.fullmatch(r"[a-z][a-z0-9-]{2,63}", str(manifest["deck_id"])):
        raise CardError("invalid deck_id")
    if type(manifest["version"]) is not int or manifest["version"] < 1:
        raise CardError("invalid manifest version")
    if manifest["source_locale"] != "en":
        raise CardError("pilot currently requires licensed English source cards")
    if manifest["license"] not in LICENSES:
        raise CardError("unapproved or unknown card-content license")
    if not isinstance(manifest["rights_holder"], str) or not manifest["rights_holder"].strip():
        raise CardError("rights_holder is required; validate ownership before publishing")

    rows = {}
    for obj in read_jsonl(source_path):
        key = obj.get("id")
        kind = obj.get("kind")
        if not isinstance(key, str) or not ID_RE.fullmatch(key) or key.split("-", 1)[0] != kind:
            raise CardError(f"invalid id/kind: {key!r}")
        expected = {"id", "kind", "text", "pick", "draw"} if kind == "prompt" else {
            "id", "kind", "text"}
        if set(obj) != expected:
            raise CardError(f"{key}: required fields {sorted(expected)} only")
        validate_text(obj["text"], key)
        if kind == "prompt":
            if type(obj["pick"]) is not int or not 1 <= obj["pick"] <= 3:
                raise CardError(f"{key}: pick must be 1..3")
            if type(obj["draw"]) is not int or not 0 <= obj["draw"] <= 2:
                raise CardError(f"{key}: draw must be 0..2")
            if obj["text"].count(PLACEHOLDER) != obj["pick"]:
                raise CardError(f"{key}: placeholder count must match pick")
        elif PLACEHOLDER in obj["text"]:
            raise CardError(f"{key}: answer must not contain placeholder")
        if key in rows:
            raise CardError(f"duplicate source id {key}")
        rows[key] = obj
        if len(rows) > MAX_CARDS:
            raise CardError("source deck exceeds card-count limit")
    if not any(x["kind"] == "prompt" for x in rows.values()) or not any(
            x["kind"] == "answer" for x in rows.values()):
        raise CardError("source must contain both prompt and answer cards")
    return manifest, rows


def source_hash(manifest, card):
    # Stable per-card binding, invalidated by changes to rights, version, text,
    # pick or draw. Additions to the pack don't invalidate unrelated cards.
    canonical = {
        "deck_id": manifest["deck_id"], "version": manifest["version"],
        "license": manifest["license"], "card": card}
    raw = json.dumps(canonical, ensure_ascii=False, sort_keys=True,
                     separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


def validate_draft(manifest, original, draft, release=False):
    key = original["id"]
    if set(draft) != {"id", "source_hash", "locale", "text", "reviewed"}:
        raise CardError(f"{key}: draft has missing/unexpected fields")
    if draft["id"] != key or draft["locale"] != "de":
        raise CardError(f"{key}: wrong card identity or target locale")
    if draft["source_hash"] != source_hash(manifest, original):
        raise CardError(f"{key}: source or rights changed; retranslation needed")
    if type(draft["reviewed"]) is not bool:
        raise CardError(f"{key}: reviewed must be boolean")
    if release and draft["reviewed"] is not True:
        raise CardError(f"{key}: unreviewed translation; refusing release")
    validate_text(draft["text"], key)
    if original["kind"] == "prompt" and (
            draft["text"].count(PLACEHOLDER) != original["pick"]):
        raise CardError(f"{key}: prompt placeholder count/order changed")
    if original["kind"] == "answer" and PLACEHOLDER in draft["text"]:
        raise CardError(f"{key}: answer unexpectedly has a placeholder")
    if draft["text"] == original["text"]:
        # This is a QA warning not a failure: names and technical phrases
        # can legitimately be identical in English and German.
        return "unchanged text: manual bilingual check required"
    return None


def load_drafts(path, manifest, source, release=False):
    result = {}
    if not Path(path).exists():
        if release:
            raise CardError("release requires complete reviewed translation file")
        return result
    for draft in read_jsonl(path):
        key = draft.get("id")
        if not isinstance(key, str) or key not in source:
            raise CardError(f"draft has unknown source id: {key!r}")
        if key in result:
            raise CardError(f"duplicate translation id {key}")
        validate_draft(manifest, source[key], draft, release=release)
        result[key] = draft
    if release and set(result) != set(source):
        raise CardError(f"release incomplete: {len(result)}/{len(source)} cards")
    return result


def translate_one(manifest, card, model, token):
    guidance = (
        "You are localizing an original/licensed English party-game card to "
        "idiomatic German (de-DE). Preserve its comedic intent and concise "
        "punchline; adapt wordplay where necessary without adding offensive "
        "content or facts not present. Output ONLY JSON with the exact field "
        "'text'. If a card is a prompt, copy each literal '____' placeholder "
        "unchanged in count and order, and keep surrounding grammar playable "
        "with German answers. No markdown, explanations or metadata. Keep "
        "numbers, proper names and technical terms intact when appropriate. "
        "Prefer native ä/ö/ü/ß and normal UTF-8 punctuation."
    )
    payload = {
        "model": model, "temperature": 0.2, "max_tokens": 500,
        "response_format": TRANSLATION_SCHEMA,
        "provider": {"require_parameters": True},
        "messages": [
            {"role": "system", "content": guidance},
            {"role": "user", "content": json.dumps({
                "deck": manifest["deck_id"], "id": card["id"],
                "kind": card["kind"], "text": card["text"],
                "pick": card.get("pick", 0), "draw": card.get("draw", 0)
            }, ensure_ascii=False)}
        ],
    }
    request = urllib.request.Request(
        OPENROUTER_URL, data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + token,
                 "HTTP-Referer": "https://github.com/0b-ivan/inx-4x-pro",
                 "X-Title": "X4 Pro CAW card translation drafts"},
        method="POST")
    with urllib.request.urlopen(request, timeout=75) as response:
        body = json.load(response)
    raw = body["choices"][0]["message"]["content"]
    if isinstance(raw, list):
        raw = "".join(c.get("text", "") for c in raw if isinstance(c, dict))
    translated = json.loads(raw)
    if not isinstance(translated, dict) or set(translated) != {"text"}:
        raise CardError("OpenRouter response violates exact text-only schema")
    return translated["text"]


def generate(manifest, source, saved, output, model, token, limit):
    if limit < 1 or limit > 20:
        raise CardError("explicit pilot limit must be 1..20 cards")
    pending = [v for k, v in source.items() if k not in saved][:limit]
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    # Append-only checkpoint. A failed API request cannot destroy old drafts.
    with Path(output).open("a", encoding="utf-8") as stream:
        for card in pending:
            for attempt in range(3):
                try:
                    text = translate_one(manifest, card, model, token)
                    draft = {"id": card["id"], "source_hash": source_hash(manifest, card),
                             "locale": "de", "text": text, "reviewed": False}
                    validate_draft(manifest, card, draft)
                    break
                except (CardError, ValueError, KeyError, TimeoutError, urllib.error.URLError) as exc:
                    if attempt == 2:
                        raise CardError(f"{card['id']}: failed after 3 attempts: {type(exc).__name__}") from exc
                    time.sleep(2 ** attempt)
            stream.write(json.dumps(draft, ensure_ascii=False, separators=(",", ":")) + "\n")
            stream.flush()
            saved[draft["id"]] = draft
            print(f"Draft checkpoint {len(saved)}/{len(source)} (unreviewed)", flush=True)
    return len(pending)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=("validate", "translate"))
    p.add_argument("--manifest", required=True)
    p.add_argument("--source", required=True)
    p.add_argument("--drafts", required=True)
    p.add_argument("--for-release", action="store_true")
    p.add_argument("--model", default="anthropic/claude-sonnet-4.6")
    p.add_argument("--limit", type=int, default=20)
    args = p.parse_args()
    try:
        manifest, source = load_source(args.manifest, args.source)
        if args.for_release and args.action != "validate":
            raise CardError("translation generation never runs in release mode")
        saved = load_drafts(args.drafts, manifest, source, release=args.for_release)
        if args.action == "validate":
            print(f"Validated {len(saved)}/{len(source)} cards; "
                  f"release gate={'passed' if args.for_release else 'not requested'}")
        else:
            token = os.environ.get("OPENROUTER_API_KEY")
            if not token:
                raise CardError("OPENROUTER_API_KEY missing (no calls made)")
            added = generate(manifest, source, saved, args.drafts, args.model, token, args.limit)
            print(f"Generated {added} UNREVIEWED translations; no publish or SD pack.")
    except (CardError, ValueError, OSError) as error:
        p.error(str(error))


if __name__ == "__main__":
    main()
