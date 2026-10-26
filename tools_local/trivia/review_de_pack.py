#!/usr/bin/env python3
"""Independent bilingual editorial QA for German trivia drafts (NOT approval).

Claude produces drafts; an independent model sees English sources and German
drafts, corrects wording or rejects untranslatable/ambiguous clues. Every
accepted rewrite is checked against the X4 Pro's actual binary/font limits.
The reviewer never sets reviewed=True and never publishes a pack.
"""
import argparse
import json
import os
import re
import time
from collections import Counter
import urllib.error
import urllib.request
from pathlib import Path

from build_de_pack import TranslationError, load_jsonl, source_hash, validate_record
from translate_de_pack import API, normalize_display_punctuation

# Numeric contradictions require evidence outside the translation model.
# The 1947 Kon-Tiki record is a real regression: the English Jeopardy clue
# says "10 days", but the historically correct duration is 101 days.
_NUMBER = re.compile(r"(?<![0-9])(?:[0-9]+(?:[.,][0-9]+)?)(?![0-9])")


def numeric_facts(value):
    if isinstance(value, list):
        return Counter(token for part in value for token in _NUMBER.findall(part))
    return Counter(_NUMBER.findall(value))


DEFAULT_MODEL = "openai/gpt-5.4"
FIELDS = ("q", "a", "alt", "w")
SYSTEM = (
    "You are an independent German-language editorial reviewer, not a translator "
    "of a new game. Compare the provided original English trivia question, "
    "its exact correct and incorrect answer options, and the German draft. "
    "Evaluate factual equivalence, natural idiomatic German, unambiguous clue, "
    "grammar, umlauts, historical and geographic accuracy, appropriate difficulty, "
    "and faithful answer/distractor semantics. "
    "If the existing German clue is already correct, natural and unambiguous, "
    "return verdict 'accept' and unchanged q/a/alt/w. Do not change for style alone. "
    "If a conservative correction can fix it without altering the intended "
    "correct answer, return 'rewrite' and the complete corrected German record. "
    "Preserve the original count and index order of alternate and wrong answers. "
    "Do not replace the correct answer with a different fact. "
    "If a pun, cultural reference, or ambiguity cannot be translated faithfully, "
    "or the original answer is questionable, return verdict 'reject', unchanged "
    "q/a/alt/w, and explain the issue for a human editor. "
    "NEVER invent facts, new answer options or unrelated clues. "
    "Use idiomatic German with ä, ö, ü, Ä, Ö, Ü, ß; keep meaningful names intact. "
    "Never turn place directions into fragmentary commands. "
    "Avoid meta-language such as 'antonym of this word' where a natural German "
    "clue can preserve the same answer. For trivia of the form 'what country is "
    "surrounded by South Africa?' with answer Lesotho, the existing natural "
    "German translation is acceptable; no mandatory rewrite. "
    "Use plain straight quotes and hyphens supported by the e-ink font. "
    "Return JSON only. 'reason' is a brief German explanation of your judgment."
)
REVIEW_FORMAT = {
    "type": "json_schema",
    "json_schema": {
        "name": "german_trivia_editorial_review",
        "strict": True,
        "schema": {
            "type": "object",
            "properties": {
                "verdict": {"type": "string", "enum": ["accept", "rewrite", "reject"]},
                "reason": {"type": "string"},
                "q": {"type": "string"},
                "a": {"type": "string"},
                "alt": {"type": "array", "items": {"type": "string"}},
                "w": {"type": "array", "items": {"type": "string"}},
            },
            "required": ["verdict", "reason", "q", "a", "alt", "w"],
            "additionalProperties": False,
        },
    },
}


def review_one(original, draft, model, api_key, timeout=100):
    payload = json.dumps({
        "original_english": {field: original[field] for field in FIELDS},
        "draft_german": {field: draft[field] for field in FIELDS},
    }, ensure_ascii=False)
    request = {
        "model": model,
        "max_tokens": 2400,
        "response_format": REVIEW_FORMAT,
        "provider": {"require_parameters": True},
        "messages": [{"role": "system", "content": SYSTEM},
                     {"role": "user", "content": payload}],
    }
    req = urllib.request.Request(
        API, data=json.dumps(request, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json",
                 "Authorization": "Bearer " + api_key,
                 "HTTP-Referer": "https://github.com/0b-ivan/inx-4x-pro",
                 "X-Title": "X4 Pro Independent Trivia Editorial Review"},
        method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        data = json.load(response)
    raw = data["choices"][0]["message"]["content"]
    if isinstance(raw, list):
        raw = "".join(part.get("text", "") for part in raw if isinstance(part, dict))
    obj = json.loads(raw)
    if not isinstance(obj, dict) or set(obj) != {"verdict", "reason", *FIELDS}:
        raise TranslationError("editor returned incorrect JSON fields")
    if obj["verdict"] not in ("accept", "rewrite", "reject"):
        raise TranslationError("invalid editorial verdict")
    if not isinstance(obj["reason"], str) or not obj["reason"].strip():
        raise TranslationError("editor omitted its reasoning")
    for field in FIELDS:
        v = obj[field]
        if field in ("alt", "w"):
            if not isinstance(v, list) or any(not isinstance(x, str) for x in v):
                raise TranslationError(f"invalid editor field {field}")
            obj[field] = [normalize_display_punctuation(x) for x in v]
        else:
            if not isinstance(v, str):
                raise TranslationError(f"invalid editor field {field}")
            obj[field] = normalize_display_punctuation(v)
    return obj


def apply_review(original, draft, decision):
    """Return unchanged or corrected *unapproved* record plus machine-only audit."""
    ident = original["id"]
    if draft.get("id") != ident or draft.get("source_hash") != source_hash(original):
        raise TranslationError(f"{ident}: draft/source identity mismatch")
    validate_record(original, draft, allow_draft=True)
    verdict = decision["verdict"]
    changes = {key: decision[key] for key in FIELDS}
    # 'accept' must mean accept AS IS, not silently edit without a revision.
    if verdict == "accept" and any(changes[k] != draft[k] for k in FIELDS):
        verdict = "rewrite"
    report = {
        "id": ident, "source_hash": draft["source_hash"],
        "verdict": verdict, "reason": decision["reason"],
        "applied": False, "needs_human_review": True,
        "english": {key: original[key] for key in FIELDS},
        "before": {key: draft[key] for key in FIELDS},
        "after": {key: draft[key] for key in FIELDS},
    }
    result = dict(draft)
    result["reviewed"] = False
    report["proposed"] = changes

    # Do not let either model silently alter Arabic numerals, or force an
    # erroneous English fact into a historically sound German translation.
    # Both cases need external evidence and a human decision.
    numerical_issues = []
    for key in FIELDS:
        original_numbers = numeric_facts(original[key])
        draft_numbers = numeric_facts(draft[key])
        proposed_numbers = numeric_facts(changes[key])
        if original_numbers != draft_numbers:
            numerical_issues.append(f"{key}: Originalzahlen {dict(original_numbers)} "
                                    f"gegenüber deutschen Zahlen {dict(draft_numbers)}")
        if draft_numbers != proposed_numbers:
            numerical_issues.append(f"{key}: vorgeschlagene Zahlenänderung "
                                    f"{dict(draft_numbers)} -> {dict(proposed_numbers)}")
    if numerical_issues:
        verdict = "reject"
        report["verdict"] = verdict
        report["reason"] += " [Zahlen/Fakten manuell prüfen: " + "; ".join(numerical_issues) + "]"

    if verdict == "rewrite":
        candidate = dict(result)
        candidate.update(changes)
        try:
            validate_record(original, candidate, allow_draft=True)
        except (ValueError, TranslationError) as exc:
            report["verdict"] = "reject"
            report["reason"] += f" [Firmwarevalidation: {exc}]"
        else:
            result = candidate
            report["after"] = changes
            report["applied"] = True
    elif verdict == "reject":
        # Never smuggle a model rewrite into a rejected draft.
        report["after"] = {key: draft[key] for key in FIELDS}
    return result, report


def review(source_path, draft_path, output_path, report_path, model, api_key, limit=20):
    sources = {item["id"]: item for item in load_jsonl(source_path)}
    drafts = list(load_jsonl(draft_path))
    if limit < 1 or len(drafts) > limit:
        raise TranslationError(f"Too many drafts: {len(drafts)} for limit {limit}")
    if not drafts:
        raise TranslationError("No German drafts found")
    if Path(output_path).resolve() == Path(draft_path).resolve():
        raise TranslationError("Keep the original draft file unchanged")
    if Path(output_path).resolve() == Path(report_path).resolve():
        raise TranslationError("Output draft and audit files must be different")
    for dest in (output_path, report_path):
        Path(dest).parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    results = {"accept": 0, "rewrite": 0, "reject": 0}
    with open(output_path, "w", encoding="utf-8") as out, \
         open(report_path, "w", encoding="utf-8") as audit:
        for draft in drafts:
            ident = draft.get("id")
            if not isinstance(ident, str) or ident in seen or ident not in sources:
                raise TranslationError(f"Unknown or repeated draft ID: {ident!r}")
            seen.add(ident)
            original = sources[ident]
            if draft.get("source_hash") != source_hash(original):
                raise TranslationError(f"{ident}: stale source hash")
            for attempt in range(2):
                try:
                    decision = review_one(original, draft, model, api_key)
                    break
                except (urllib.error.URLError, TimeoutError, ValueError, KeyError) as exc:
                    if attempt:
                        raise TranslationError(f"{ident}: review call failed: {exc}") from exc
                    time.sleep(2)
            corrected, report = apply_review(original, draft, decision)
            report["model"] = model
            results[report["verdict"]] += 1
            out.write(json.dumps(corrected, ensure_ascii=False, separators=(",", ":")) + "\n")
            audit.write(json.dumps(report, ensure_ascii=False, separators=(",", ":")) + "\n")
            out.flush()
            audit.flush()
            print(f"Editorial {len(seen)}/{len(drafts)}: {report['verdict']} {ident}", flush=True)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="Original English questions JSONL")
    ap.add_argument("--drafts", required=True, help="Original German drafts JSONL")
    ap.add_argument("--out", required=True, help="Revised drafts JSONL (not reviewed)")
    ap.add_argument("--report", required=True, help="Machine editorial verdicts JSONL")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--limit", type=int, default=20, help="Maximum reviewed questions")
    args = ap.parse_args()
    key = os.getenv("OPENROUTER_API_KEY")
    if not key:
        ap.error("Set the temporary GitHub Actions OPENROUTER_API_KEY secret")
    try:
        result = review(args.src, args.drafts, args.out, args.report,
                        args.model, key, args.limit)
    except TranslationError as exc:
        ap.error(str(exc))
    print(f"Machine editorial QA (not human approved): {result}", flush=True)


if __name__ == "__main__":
    main()
