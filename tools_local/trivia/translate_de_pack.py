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


def translate_one(item, model, api_key, timeout=100, extra_instruction=""):
    user = {k: item.get(k, [] if k in ("alt", "w") else "")
            for k in ("q", "a", "alt", "w")}
    request = {
        "model": model,
        "temperature": 0,
        "max_tokens": 2048,
        "response_format": TRANSLATION_RESPONSE_FORMAT,
        "provider": {"require_parameters": True},
        "messages": [{"role": "system", "content": SYSTEM + ("\n" + extra_instruction if extra_instruction else "")},
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
    # Optional bounded, auditable usage ledger for one-shot experiments.
    ledger_path = os.getenv("TRIVIA_USAGE_LOG")
    if ledger_path and os.getenv("TRIVIA_SPEND_LIMIT_USD"):
        spent = sum(float(r.get("cost_usd", 0)) for r in load_jsonl(ledger_path)) if Path(ledger_path).exists() else 0.0
        if spent >= float(os.environ["TRIVIA_SPEND_LIMIT_USD"]):
            raise TranslationError("Translation spending guard reached; no further API calls")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        result = json.load(response)
    if ledger_path:
        usage = result.get("usage") or {}
        input_tokens = int(usage.get("prompt_tokens") or 0)
        output_tokens = int(usage.get("completion_tokens") or 0)
        # Conservative fallback for this pilot's Qwen provider, not a billing claim.
        estimated = max(.0002, (input_tokens * .30 + output_tokens * 1.00) / 1_000_000)
        cost = float(usage["cost"]) if usage.get("cost") is not None else estimated
        with open(ledger_path, "a", encoding="utf-8") as ledger:
            ledger.write(json.dumps({"model": model, "input_tokens": input_tokens,
                                     "output_tokens": output_tokens, "cost_usd": cost,
                                     "is_estimate": usage.get("cost") is None}) + "\n")
    raw = result["choices"][0]["message"]["content"]
    if isinstance(raw, list):
        raw = "".join(part.get("text", "") for part in raw if isinstance(part, dict))
    raw = raw.strip()
    if raw.startswith("```"):
        raw = raw.split("\n", 1)[1].rsplit("```", 1)[0].strip()
    translated = json.loads(raw)
    if not isinstance(translated, dict) or set(translated) != {"q", "a", "alt", "w"}:
        raise TranslationError("Model response violates the expected JSON schema")
    return {
        key: ([normalize_display_punctuation(x) for x in value]
              if isinstance(value, list) else normalize_display_punctuation(value))
        for key, value in translated.items()
    }



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

def generate(source_path, output_path, model, api_key, limit=20, all_questions=False):
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
    # Excluded IDs are explicitly tracked (and source-validated), preventing
    # previously rejected display-incompatible candidates from being charged
    # again when restoring a checkpoint. No exclusions in full-corpus mode.
    rejected_path = os.getenv("TRIVIA_REJECTS_FILE")
    prior_rejected = set()
    if rejected_path and Path(rejected_path).exists():
        for item in load_jsonl(rejected_path):
            ident = item.get("id")
            if not isinstance(ident, str) or not ident or ident in prior_rejected:
                raise TranslationError("invalid or duplicate rejected source ID")
            prior_rejected.add(ident)
    if prior_rejected - set(source_by_id):
        raise TranslationError("rejection checkpoint contains IDs absent from source")
    if prior_rejected & set(done):
        raise TranslationError("translation and rejection checkpoints overlap")
    if all_questions and prior_rejected:
        raise TranslationError("full-corpus build must not exclude source questions")
    generated = 0
    skipped = 0
    # The fixed 10-skip threshold worked for 20-item pilots, but incorrectly
    # aborted large quality samples. Preserve a 20% failure ceiling per run.
    max_skipped = max(10, math.ceil(limit * 0.20))
    # In a pilot, allow spare diverse candidates to replace questions whose
    # correct answer cannot fit the actual e-ink fonts. Full corpus runs fail
    # rather than silently omitting content.
    candidates = (source if all_questions else
                  pilot_candidates(source, min(len(source), limit * 3 + 10), set(done) | prior_rejected))
    with output.open("a", encoding="utf-8") as out:
        for original in candidates:
            ident = original["id"]
            if ident in done:
                if done[ident].get("source_hash") != source_hash(original):
                    raise TranslationError(f"source changed since draft for {ident}")
                continue
            if ident in prior_rejected:
                continue
            if not all_questions and generated >= limit:
                break
            accepted = False
            for attempt in range(4):
                try:
                    translated = translate_one(original, model, api_key)
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
                        if rejected_path:
                            with open(rejected_path, "a", encoding="utf-8") as rejected:
                                rejected.write(json.dumps({
                                    "id": ident, "source_hash": source_hash(original),
                                    "reason": str(error)[:300]}, ensure_ascii=False) + "\n")
                        if skipped > max_skipped:
                            raise TranslationError(
                                f"Too many rejected pilot questions: {skipped}/{max_skipped}; "
                                "partial translations and rejection checkpoints preserved"
                            ) from error
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
    args = ap.parse_args()
    if args.limit is not None and args.limit <= 0:
        ap.error("--limit must be positive")
    key = os.getenv("OPENROUTER_API_KEY")
    if not key:
        ap.error("OPENROUTER_API_KEY environment variable is required")
    try:
        n, done, total = generate(args.src, args.out, args.model, key, args.limit, args.all)
    except TranslationError as e:
        ap.error(str(e))
    print(f"Added {n} unchecked drafts; {done}/{total} saved. Review before releasing.")


if __name__ == "__main__":
    main()
