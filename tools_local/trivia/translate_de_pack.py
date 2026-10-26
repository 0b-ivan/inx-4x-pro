#!/usr/bin/env python3
"""Create resumable draft German translations using an OpenRouter chat model.

No translation is marked reviewed. Use build_de_pack.py only after QA. Run a
small sample first: --limit 20 is the default; --all is an explicit opt-in for
a potentially expensive full-corpus translation. Never commit API keys.
"""
import argparse
import json
import math
import os
import time
import urllib.error
import urllib.request
from pathlib import Path

from build_de_pack import TranslationError, load_jsonl, source_hash, validate_record
from german_orthography import normalize_orthography

API = "https://openrouter.ai/api/v1/chat/completions"
# OpenRouter supports a native strict JSON schema for Claude Sonnet 4.6.
# Constrained decoding prevents malformed JSON from wasting paid retries.
TRANSLATION_RESPONSE_FORMAT = {
    "type": "json_schema",
    "json_schema": {
        "name": "translated_trivia_question",
        "strict": True,
        "schema": {
            "type": "object",
            "properties": {
                "q": {"type": "string", "description": "Complete translated German question"},
                "a": {"type": "string", "description": "Correct answer in German"},
                "alt": {"type": "array", "items": {"type": "string"},
                        "description": "Accepted alternative answers in source order"},
                "w": {"type": "array", "items": {"type": "string"},
                      "description": "Incorrect options in source order"},
            },
            "required": ["q", "a", "alt", "w"],
            "additionalProperties": False,
        },
    },
}
# The e-ink reading font contains German letters, but omits smart
# punctuation. Normalize only typography, never spelling, facts or answers.
_DISPLAY_PUNCTUATION = str.maketrans({
    "\u201c": '"', "\u201d": '"', "\u201e": '"',
    "\u201f": '"', "\u00ab": '"', "\u00bb": '"',
    "\u2018": "'", "\u2019": "'", "\u201a": "'",
    "\u2013": "-", "\u2014": "-", "\u2212": "-",
    "\u2026": "...", "\u00a0": " ", "\u202f": " ",
    "\u2009": " ", "\u200b": "",
})


def normalize_display_punctuation(value):
    if not isinstance(value, str):
        return value
    # Keep umlauts, ß and all supported accented letters in UTF-8.
    # Only typographic punctuation not available in the device font is ASCII.
    return normalize_orthography(value.translate(_DISPLAY_PUNCTUATION))


SYSTEM = (
    "You are a meticulous German localization editor for a factual trivia game. "
    "Translate the complete English trivia record into idiomatic, concise German. "
    "Return ONLY a JSON object: q and a must be strings; alt and w must be arrays "
    "of strings with exactly the same counts and option order as their source. "
    "Preserve the original question's intended meaning, difficulty, tense, "
    "historical period, implicit clues and single correct answer. "
    "Do not answer the question within its German wording. "
    "Translate all answer candidates in context, keeping proper nouns and "
    "official titles intact when appropriate. Every wrong option must remain "
    "wrong, clearly distinct from the accepted primary and alternate answers. "
    "Preserve dates, quantities, geographic references and correct spelling. "
    "Never invent facts or substitute an unrelated question. For idioms or "
    "wordplay, preserve the original clue-answer relationship or fail instead "
    "of fabricating a new fact. Use natural German with correct German " 
    "orthography: ALWAYS write ä, ö, ü, Ä, Ö, Ü and ß wherever required. " 
    "Do not omit dots or transliterate these letters into ae/oe/ue or " 
    "plain a/o/u. Examples: München (not Munchen or Muenchen), " 
    "Dänemark (not Danemark), Könige (not Konige), Nobelpreisträger " 
    "(not Nobelpreistrager), früh (not fruh), später (not spaeter). "
    "Prefer short phrasing that fits a small e-ink display without removing "
    "information needed to solve the question. Do not include explanations, "
    "metadata or markdown fences. Use plain straight ASCII quotation marks "
    "and hyphens instead of typographic quotes, en dashes or em dashes; "
    "the e-ink device's reading font does not support those punctuation glyphs."
)


def translate_one(item, model, api_key, timeout=100, track_cost=False):
    user = {k: item.get(k, [] if k in ("alt", "w") else "")
            for k in ("q", "a", "alt", "w")}
    request = {
        "model": model,
        "temperature": 0,
        "max_tokens": 2048,
        "response_format": TRANSLATION_RESPONSE_FORMAT,
        "provider": {"require_parameters": True},
        "messages": [{"role": "system", "content": SYSTEM},
                     {"role": "user", "content": json.dumps(user, ensure_ascii=False)}],
    }
    data = json.dumps(request, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        API, data=data,
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + api_key,
                 "HTTP-Referer": "https://github.com/0b-ivan/inx-4x-pro",
                 "X-Title": "X4 Pro German Trivia Pack"},
        method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        result = json.load(response)
    raw = result["choices"][0]["message"]["content"]
    if isinstance(raw, list):
        raw = "".join(part.get("text", "") for part in raw if isinstance(part, dict))
    raw = raw.strip()
    if raw.startswith("```"):
        raw = raw.split("\n", 1)[1].rsplit("```", 1)[0].strip()
    translated = json.loads(raw)
    if not isinstance(translated, dict) or set(translated) != {"q", "a", "alt", "w"}:
        raise TranslationError("Model response violates the expected JSON schema")
    cleaned = {key: ([normalize_display_punctuation(x) for x in value]
              if isinstance(value, list) else normalize_display_punctuation(value))
        for key, value in translated.items()}
    if not track_cost:
        return cleaned
    cost = result.get("usage", {}).get("cost")
    if cost is None or not isinstance(cost, (int, float)) or not math.isfinite(cost) or cost < 0:
        raise TranslationError("OpenRouter did not report a valid USD usage.cost; fail closed")
    return cleaned, float(cost)



def pilot_candidates(source, limit, already_done=()):
    """Deterministically sample the entire pack, avoiding repeated answers.

    Source packs are grouped by the correct answer. Taking the first 20
    therefore produced ten AUSTRALIA and ten CHINA questions. Spread the pilot
    across the whole file and prefer distinct answers; never reshuffle the
    source when translating the complete corpus with --all.
    """
    done = set(already_done)
    if limit <= 0:
        return []
    n = len(source)
    if n == 0:
        return []
    # A coprime stride visits every position exactly once, without shuffling
    # or allocating a duplicate 50k-record list.
    stride = max(1, (n * 61803398875 // 100000000000) | 1)
    while math.gcd(n, stride) != 1:
        stride += 2
    seen_answers = set()
    chosen = []
    for j in range(n):
        row = source[(n // 3 + j * stride) % n]
        if row["id"] in done:
            continue
        answer = row.get("a", "").casefold().strip()
        if not answer or answer in seen_answers:
            continue
        seen_answers.add(answer)
        chosen.append(row)
        if len(chosen) == limit:
            return chosen
    # Small synthetic/test packs may contain fewer unique answers than slots.
    for j in range(n):
        row = source[(n // 3 + j * stride) % n]
        if row["id"] not in done and row not in chosen:
            chosen.append(row)
            if len(chosen) == limit:
                break
    return chosen

def generate(source_path, output_path, model, api_key, limit=20, all_questions=False, max_cost_usd=None):
    source = list(load_jsonl(source_path))
    done = {}
    output = Path(output_path)
    if output.exists():
        for row in load_jsonl(output):
            ident = row.get("id")
            if ident in done:
                raise TranslationError(f"duplicate saved translation {ident}")
            done[ident] = row
    source_by_id = {row["id"]: row for row in source}
    if set(done) - set(source_by_id):
        raise TranslationError("saved draft has IDs absent from this source")
    for ident, old in done.items():
        if old.get("source_hash") != source_hash(source_by_id[ident]):
            raise TranslationError(f"source changed since draft for {ident}")
    generated = 0
    skipped = 0
    spent = 0.0
    # Reserve conservatively for the next request; account-level limits remain authoritative.
    request_reserve = 0.10
    # In a pilot, allow spare diverse candidates to replace questions whose
    # correct answer cannot fit the actual e-ink fonts. Full corpus runs fail
    # rather than silently omitting content.
    candidates = (source if all_questions else
                  pilot_candidates(source, min(len(source), limit * 3 + 10), done))
    with output.open("a", encoding="utf-8") as out:
        for original in candidates:
            if max_cost_usd is not None and spent + request_reserve > max_cost_usd:
                print(f"Budget stop: reported USD {spent:.6f} of {max_cost_usd:.2f}; drafts saved.", flush=True)
                return generated, len(done), len(source)
            ident = original["id"]
            if ident in done:
                if done[ident].get("source_hash") != source_hash(original):
                    raise TranslationError(f"source changed since draft for {ident}")
                continue
            if not all_questions and generated >= limit:
                break
            accepted = False
            for attempt in range(4):
                try:
                    if max_cost_usd is None:
                        translated = translate_one(original, model, api_key)
                    else:
                        translated, cost = translate_one(original, model, api_key, track_cost=True)
                        spent += cost
                    if max_cost_usd is not None and spent > max_cost_usd:
                        raise TranslationError(f"USD budget exceeded after request: {spent:.6f}")
                    # Accept only content fields. Provider-supplied IDs or
                    # approval flags may not override our source and review gate.
                    row = {"id": ident, "source_hash": source_hash(original),
                           **{k: translated[k] for k in ("q", "a", "alt", "w")},
                           "reviewed": False}
                    validate_record(original, row, allow_draft=True)
                    accepted = True
                    break
                except (KeyError, ValueError, TranslationError) as error:
                    # Only pilots may replace a question that is unsuitable
                    # for the display, after one retry. Record why it was
                    # rejected; never disguise missing translations as a pack.
                    if not all_questions and attempt >= 1:
                        skipped += 1
                        print(f"Rejected pilot question {ident}: {error}", flush=True)
                        if skipped > 10:
                            raise TranslationError("Too many rejected pilot questions") from error
                        break
                    if attempt == 3:
                        raise TranslationError(
                            f"{ident}: validation failed after 4 tries: {error}"
                        ) from error
                    time.sleep(min(20, 2 ** attempt))
                except (urllib.error.URLError, TimeoutError) as error:
                    # Authentication, rate limits and connectivity are not
                    # invalid questions. Do not skip and hide API outages.
                    if attempt == 3:
                        raise TranslationError(
                            f"{ident}: OpenRouter failed after 4 tries: {error}"
                        ) from error
                    time.sleep(min(20, 2 ** attempt))
            if not accepted:
                continue
            out.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")
            out.flush()  # resume after interruption without losing completed calls
            done[ident] = row
            generated += 1
            print(f"Draft translations: {len(done)}/{len(source)}", flush=True)
    if not all_questions and generated != limit:
        raise TranslationError(
            f"Pilot produced only {generated}/{limit} requested questions; {skipped} rejected"
        )
    if skipped:
        print(f"Pilot skipped {skipped} display-invalid questions (no releases).", flush=True)
    return generated, len(done), len(source)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="Curated English pack.jsonl")
    ap.add_argument("--out", required=True, help="Checkpointed German draft JSONL")
    ap.add_argument("--model", required=True, help="OpenRouter model ID")
    scope = ap.add_mutually_exclusive_group()
    scope.add_argument("--limit", type=int, default=20, help="New questions (default 20)")
    scope.add_argument("--all", action="store_true", help="Opt into translating every remaining item")
    ap.add_argument("--max-cost-usd", type=float, default=None)
    args = ap.parse_args()
    if args.limit is not None and args.limit <= 0:
        ap.error("--limit must be positive")
    if args.max_cost_usd is not None and (not math.isfinite(args.max_cost_usd) or args.max_cost_usd <= 0):
        ap.error("--max-cost-usd must be positive and finite")
    key = os.getenv("OPENROUTER_API_KEY")
    if not key:
        ap.error("OPENROUTER_API_KEY environment variable is required")
    try:
        n, done, total = generate(args.src, args.out, args.model, key, args.limit, args.all, args.max_cost_usd)
    except TranslationError as e:
        ap.error(str(e))
    print(f"Added {n} unchecked drafts; {done}/{total} saved. Review before releasing.")


if __name__ == "__main__":
    main()
