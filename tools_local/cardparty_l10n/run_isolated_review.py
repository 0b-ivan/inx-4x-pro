#!/usr/bin/env python3
"""One-time, independent OpenRouter content/humor review. Never autoapprove."""
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools_local/cardparty_l10n"))
from translate_cards import load_source, load_drafts, validate_draft

SOURCE = ROOT / "tools_local/cardparty_l10n/examples/cards-en.jsonl"
MANIFEST = ROOT / "tools_local/cardparty_l10n/examples/deck.json"
DRAFTS = ROOT / "tools_local/cardparty_l10n/examples/cards-de-editorial-proposed.jsonl"
REPORT = Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "cardparty-independent-semantic-review.jsonl"
CANDIDATES = REPORT.with_name("cardparty-independent-approved-for-human-review.jsonl")
MODEL = "openai/gpt-5.4"
DIMENSIONS = ("meaning", "humor", "domain", "grammar", "composition")
OPTIONS = ("pass", "fail", "uncertain")
SCHEMA = {
  "type": "json_schema", "json_schema": {
    "name": "cardparty_independent_review", "strict": True,
    "schema": {"type": "object", "properties": {
      "source_meaning": {"type": "string"},
      "humor_mechanism": {"type": "string"},
      "technical_context": {"type": "string"},
      "ratings": {"type": "object",
        "properties": {k: {"type": "string", "enum": list(OPTIONS)} for k in DIMENSIONS},
        "required": list(DIMENSIONS), "additionalProperties": False},
      "decision": {"type": "string", "enum": ["accept", "revise", "manual_review"]},
      "proposed_text": {"type": "string"},
      "issues": {"type": "array", "items": {"type": "string"}},
      "explanation": {"type": "string"}
    }, "required": ["source_meaning", "humor_mechanism", "technical_context",
                      "ratings", "decision", "proposed_text", "issues", "explanation"],
       "additionalProperties": False}
  }
}
PROMPT = """Independent German bilingual editorial review of an original licensed
absurdist IT/sysadmin party card. You DID NOT create this translation.
First reason about the exact ORIGINAL meaning, technical/domain referent and
humor mechanism. Then evaluate the German candidate for five dimensions:
meaning, humor, domain, grammar and composition. Rate all pass/fail/uncertain
for the FINAL proposed_text. If any uncertainty or failure remains,
decision=manual_review and clearly explain. If accept, do not change the text
and give no issues. If revise, repair issues, explicitly describe them, and
only mark pass when confident that the proposed_text preserves original sense,
comedic incongruity and timing. Do not blindly optimize just grammar. Never
invent facts, a new joke or a new character. Respect original numbers/timing
and literal '____' placeholders. Prompt blanks must match Pick 1/2/3 and must
work with uninflected nominative noun-phrase answer cards. Answer cards must
be nominative phrases WITHOUT terminal punctuation. Pick-2/3 order matters.
In the tech deck: an alarm that ONLY rings on public holidays is a
misconfigured monitoring ALARM that goes off exclusively on holidays,
not a bedside alarm clock/Wecker; don't weaken 'only' to 'sometimes/also'.
If true semantic equivalence is uncertain, flag manual_review.
Output valid JSON matching the supplied schema only."""

def digest(row):
    payload = {key: row[key] for key in ("id", "source_hash", "locale", "text")}
    raw = json.dumps(payload, ensure_ascii=False, sort_keys=True,
                     separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()

def ask(key, card, candidate, all_sources):
    input_data = {
        "source": card, "german_candidate": candidate["text"],
        "deck_genre": "absurd IT, Linux/sysadmin, office party game",
        "deck_prompts": [c["text"] for c in all_sources.values()
                         if c["kind"] == "prompt"],
        "deck_answers": [c["text"] for c in all_sources.values()
                         if c["kind"] == "answer"],
    }
    # GPT-5.4 uses provider-specific reasoning parameters. Strict provider
    # parameter matching previously caused HTTP 404 (no matching endpoint).
    # Retain JSON schema preference but let OpenRouter select a compatible
    # backend; our local validator rejects nonconforming responses.
    payload = {"model": MODEL, "max_tokens": 1800,
               "response_format": SCHEMA,
               "messages": [
                  {"role": "system", "content": PROMPT},
                  {"role": "user", "content": json.dumps(input_data, ensure_ascii=False)}
               ]}
    request = urllib.request.Request(
        "https://openrouter.ai/api/v1/chat/completions",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": "Bearer " + key, "Content-Type": "application/json",
                 "HTTP-Referer": "https://github.com/0b-ivan/inx-4x-pro",
                 "X-Title": "CAW Independent Semantic Editorial Audit"},
        method="POST")
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=100) as response:
                result = json.load(response)
            content = result["choices"][0]["message"]["content"]
            if isinstance(content, list):
                content = "".join(x.get("text", "") for x in content
                                  if isinstance(x, dict))
            return json.loads(content)
        except (urllib.error.HTTPError, urllib.error.URLError,
                TimeoutError, ValueError, KeyError) as error:
            status = getattr(error, "code", None)
            if isinstance(error, urllib.error.HTTPError):
                # Log only the provider's status and bounded sanitized error
                # type/message; never print Authorization headers or API key.
                raw = error.read(4096).decode("utf-8", errors="replace")
                try:
                    problem = json.loads(raw).get("error", {})
                    clue = str(problem.get("message", "unknown error"))
                except (ValueError, AttributeError):
                    clue = "provider returned no JSON error details"
                clue = clue.replace(key, "[REDACTED]")
                clue = re.sub(r"sk-[A-Za-z0-9_-]+", "[REDACTED]", clue)
                print(f"MODEL_HTTP_STATUS {status}: {clue[:260]}", flush=True)
            if status in (400, 401, 402, 403, 404, 422) or attempt == 2:
                raise RuntimeError(f"OpenRouter failed on {card['id']}: "
                                   f"{type(error).__name__} status={status}") from None
            time.sleep(2 ** attempt)

def validate(source, candidate, review):
    if not isinstance(review, dict) or set(review) != {
       "source_meaning", "humor_mechanism", "technical_context",
       "ratings", "decision", "proposed_text", "issues", "explanation"}:
        raise ValueError("review object violates schema")
    ratings = review["ratings"]
    if not isinstance(ratings, dict) or set(ratings) != set(DIMENSIONS):
        raise ValueError("missing rubric fields")
    if any(rating not in OPTIONS for rating in ratings.values()):
        raise ValueError("invalid rubric rating")
    if not isinstance(review["issues"], list):
        raise ValueError("issues is not an array")
    for field in ("source_meaning", "humor_mechanism", "technical_context", "explanation"):
        if not isinstance(review[field], str) or not review[field].strip():
            raise ValueError(f"missing editorial context: {field}")
    value = review["proposed_text"]
    if not isinstance(value, str) or not value.strip():
        raise ValueError("missing review proposal")
    proposed = dict(candidate, text=value, reviewed=False)
    validate_draft(
        manifest, source, proposed
    )
    if source["kind"] == "answer" and value.rstrip().endswith((".", "?", "!")):
        raise ValueError("terminal punctuation in answer")
    if "public holidays" in source["text"].lower() and "alarm" in source["text"].lower():
        # Keep German ß intact: casefold() turns "ausschließlich" into
        # "ausschliesslich", which the required-qualification regex missed.
        low = value.lower()
        if "wecker" in low or "feiertag" not in low or not re.search(
                r"\b(nur|ausschließlich|einzig|bloß)\b", low):
            raise ValueError("holiday-only monitoring alarm sense lost")
        if re.search(r"\bnicht\s+nur\b", low):
            raise ValueError("exclusive timing inverted")
    if review["decision"] == "accept":
        if value != candidate["text"] or review["issues"]:
            raise ValueError("silent rewrite of accepted card")
    if review["decision"] == "revise" and not review["issues"]:
        raise ValueError("unexplained rewrite")
    if review["decision"] != "manual_review" and any(
            grade != "pass" for grade in ratings.values()):
        raise ValueError("unresolved issue cannot pass independent audit")
    return proposed

if __name__ == "__main__":
    key = os.getenv("OPENROUTER_API_KEY")
    if not key:
        raise SystemExit("Missing OPENROUTER_API_KEY repository secret")
    manifest, sources = load_source(MANIFEST, SOURCE)
    drafts = load_drafts(DRAFTS, manifest, sources)
    if len(sources) != 20 or set(sources) != set(drafts):
        raise SystemExit("Exact 20-card original pilot required")
    # Resume completed model reviews from a previous GitHub Actions artifact
    # without spending again on unchanged source/German cards.
    totals = {"accept": 0, "revise": 0, "manual_review": 0}
    processed = set()
    if REPORT.exists():
        valid_reviews = []
        for line in REPORT.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            ident = row["id"]
            if ident in processed or ident not in drafts or row["model"] != MODEL or (
                    row["human_approved"] is not False):
                raise SystemExit("Invalid or duplicated prior semantic review")
            # An updated candidate needs a FRESH independent model review.
            # Older unrelated evaluations remain reusable and never autobind
            # to a changed German text.
            if row["source_hash"] != drafts[ident]["source_hash"] or (
                    row["candidate_hash"] != digest(drafts[ident])):
                print(f"STALE_REVIEW {ident}: checking corrected text afresh", flush=True)
                continue
            # Do not reuse a previous manual verdict created by the
            # buggy local Unicode guard. Independent model review gets a
            # fresh chance to pass the now-corrected local validation.
            issues = row["model_review"].get("issues", [])
            if row["model_review"]["decision"] == "manual_review" and any(
                    "local_validation:ValueError:holiday-only" in str(i)
                    for i in issues):
                print(f"RECHECK_UNICODE_GUARD {ident}", flush=True)
                continue
            processed.add(ident)
            valid_reviews.append(row)
            totals[row["model_review"]["decision"]] += 1
        REPORT.write_text("".join(json.dumps(row, ensure_ascii=False) + "\n"
                                  for row in valid_reviews), encoding="utf-8")
        CANDIDATES.write_text("".join(
            json.dumps(dict(drafts[row["id"]],
                            text=row["model_review"]["proposed_text"], reviewed=False),
                       ensure_ascii=False) + "\n"
            for row in valid_reviews
            if row["model_review"]["decision"] != "manual_review"),
            encoding="utf-8")
    with REPORT.open("a", encoding="utf-8") as output, CANDIDATES.open(
            "a", encoding="utf-8") as candidates:
        for card_id, card in sources.items():
            if card_id in processed:
                continue
            draft = drafts[card_id]
            result = ask(key, card, draft, sources)
            try:
                proposal = validate(card, draft, result)
            except (ValueError, KeyError, TypeError) as error:
                # An uncertain/inconsistent model verdict must not terminate
                # the entire paid audit. Capture its raw diagnosis as MANUAL
                # and DO NOT include the suggestion in exportable proposals.
                print(f"MANUAL {card_id}: local semantic validation rejected proposal", flush=True)
                if not isinstance(result, dict):
                    raise
                result["decision"] = "manual_review"
                issues = result.get("issues")
                if not isinstance(issues, list):
                    issues = []
                issues.append(f"local_validation:{type(error).__name__}:{str(error)[:120]}")
                result["issues"] = issues
                proposal = draft
            item = {
                "id": card_id, "source_hash": draft["source_hash"],
                "candidate_hash": digest(draft), "model": MODEL,
                "model_review": result, "human_approved": False}
            output.write(json.dumps(item, ensure_ascii=False) + "\n")
            output.flush()
            totals[result["decision"]] += 1
            if result["decision"] != "manual_review":
                candidates.write(json.dumps(proposal, ensure_ascii=False) + "\n")
                candidates.flush()
            print(f"REVIEW 20: {card_id} = {result['decision']}", flush=True)
    print(f"RESULT {sum(totals.values())}/20 {totals}, independent GPT-5.4 only; human approval REQUIRED")
