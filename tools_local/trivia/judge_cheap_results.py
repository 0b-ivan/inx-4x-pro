#!/usr/bin/env python3
"""Blind-editor comparison using SAVED translations; does not translate again.

Reads the comparison-per-question.jsonl from run 40317248535, reuses the
three German candidates per source, and requests only 20 independent reviews.
No question is marked human-reviewed or published.
"""
import json
import os
import random
import sys
import urllib.error
import urllib.request
from collections import Counter
from pathlib import Path

root = Path(os.environ["TRIVIA_JUDGE_DIR"])
src = root / "comparison-per-question.jsonl"
rows = [json.loads(line) for line in src.read_text(encoding="utf-8").splitlines() if line.strip()]
if len(rows) != 20 or len({row["id"] for row in rows}) != 20:
    raise SystemExit("Expected exactly 20 unique previously translated questions")
fields = ("q", "a", "alt", "w")
for row in rows:
    if any(not isinstance(row.get(k), dict) for k in ("english", "baseline", "translations")):
        raise SystemExit("Invalid saved comparison schema")
    if any(not isinstance(row["translations"].get(m), dict) for m in ("qwen", "mini")):
        raise SystemExit("Refusing incomplete saved model candidates")
    for candidate in [row["baseline"], row["translations"]["qwen"], row["translations"]["mini"]]:
        if any(k not in candidate for k in fields):
            raise SystemExit("A saved candidate is missing required fields")

# Keep the exact OpenRouter request shape that succeeded in the earlier
# independent editorial pilot: no temperature for reasoning GPT-5.4, no
# unnecessary usage.include parameter, strict JSON schema and max_tokens.
schema = {
    "type": "json_schema",
    "json_schema": {
        "name": "trivia_de_blind_editor_results",
        "strict": True,
        "schema": {
            "type": "object",
            "properties": {
                "A": {"type": "integer"}, "B": {"type": "integer"}, "C": {"type": "integer"},
                "best": {"type": "string", "enum": ["A", "B", "C", "tie"]},
                "reason": {"type": "string"}
            },
            "required": ["A", "B", "C", "best", "reason"],
            "additionalProperties": False
        }
    }
}
system = (
    "You are an independent bilingual trivia copy editor. Compare three "
    "anonymized German versions with the English original question, correct "
    "answer, alternates and distractors. Assign each candidate A/B/C an "
    "integer score from 0 to 5 for factual and semantic faithfulness, "
    "correctness of alternatives, natural German, historical/numeric accuracy "
    "and no accidental answer disclosure. Give a short German reason and "
    "pick A/B/C or tie when warranted. You do not know model identities. "
    "Return only strict JSON."
)
key = os.environ.get("OPENROUTER_API_KEY")
if not key:
    raise SystemExit("OPENROUTER_API_KEY is required")
spend = 0.0
usage_total = {"input_tokens": 0, "output_tokens": 0, "calls": 0}
out_path = root / "judge-only-20-results.jsonl"

def call_editor(english, anonymized):
    global spend
    if usage_total["calls"] >= 20 or spend >= 0.85:
        raise RuntimeError("Budget guard stopped additional editor calls")
    request = {
        "model": "openai/gpt-5.4",
        "max_tokens": 2400,
        "response_format": schema,
        "provider": {"require_parameters": True},
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": json.dumps({
                "original_english": english, "candidate_german": anonymized
            }, ensure_ascii=False)}
        ]
    }
    req = urllib.request.Request(
        "https://openrouter.ai/api/v1/chat/completions",
        data=json.dumps(request, ensure_ascii=False).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + key,
            "HTTP-Referer": "https://github.com/0b-ivan/inx-4x-pro",
            "X-Title": "X4 Pro German Trivia Blind Editorial Comparison"
        },
        method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as response:
            payload = json.load(response)
    except urllib.error.HTTPError as error:
        # The endpoint can return useful JSON explaining why no providers
        # qualify. Never print headers, API secrets or full request bodies.
        body = error.read(2000).decode("utf-8", errors="replace")
        try:
            detail = json.loads(body).get("error", {}).get("message", body)
        except (ValueError, AttributeError):
            detail = body
        raise RuntimeError(f"OpenRouter returned HTTP {error.code}: {str(detail)[:300]}") from error

    u = payload.get("usage") or {}
    pt = int(u.get("prompt_tokens") or 0)
    ct = int(u.get("completion_tokens") or 0)
    usage_total["input_tokens"] += pt
    usage_total["output_tokens"] += ct
    usage_total["calls"] += 1
    reported = u.get("cost")
    # If cost is absent, estimate at a conservative higher rate than the
    # model's published input/output prices, so the budget guard remains useful.
    price = float(reported) if reported is not None else max(.0005, (pt * 3.5 + ct * 20) / 1_000_000)
    spend += price

    raw = payload["choices"][0]["message"]["content"]
    if isinstance(raw, list):
        raw = "".join(part.get("text", "") for part in raw if isinstance(part, dict))
    value = json.loads(raw)
    if (not isinstance(value, dict) or set(value) != {"A", "B", "C", "best", "reason"}
        or any(type(value[k]) is not int or value[k] not in range(6) for k in "ABC")
        or value["best"] not in ("A", "B", "C", "tie")
        or not isinstance(value["reason"], str) or not value["reason"].strip()):
        raise ValueError("Editor response violates strict grading schema")
    return value

with out_path.open("w", encoding="utf-8") as output:
    for i, row in enumerate(rows, 1):
        order = ["qwen", "mini", "baseline"]
        random.Random(20261026 + i).shuffle(order)
        candidates = {
            label: {k: (row["baseline"] if name == "baseline"
                        else row["translations"][name])[k] for k in fields}
            for label, name in zip("ABC", order)
        }
        try:
            judge = call_editor(row["english"], candidates)
        except Exception as error:
            # Fail loudly. Never publish a green run containing 20 failed judges.
            print(f"EDITOR_FAILURE question={i} id={row['id']}: {type(error).__name__}: {error}", flush=True)
            raise
        by_model = {name: judge[label] for label, name in zip("ABC", order)}
        winner = order["ABC".index(judge["best"])] if judge["best"] != "tie" else "tie"
        output.write(json.dumps({
            "id": row["id"], "scores": by_model,
            "winner": winner, "reason": judge["reason"],
            "qwen_numeric_warnings": row["issues"].get("qwen", []),
            "mini_numeric_warnings": row["issues"].get("mini", [])
        }, ensure_ascii=False) + "\n")
        output.flush()
        print(f"EDITOR_PROGRESS {i}/20 winner={winner} scores={by_model} estimated_spend_usd={spend:.5f}", flush=True)

results = [json.loads(line) for line in out_path.read_text(encoding="utf-8").splitlines()]
if len(results) != 20:
    raise RuntimeError("Incomplete blind-editor results")
models = ("qwen", "mini", "baseline")
summary = {
    "source_run": 40317248535,
    "questions_reviewed": 20,
    "judge_model": "openai/gpt-5.4",
    "reported_or_conservative_usd": round(spend, 6),
    "token_usage": usage_total,
    "models": {}
}
for model in models:
    scores = [r["scores"][model] for r in results]
    summary["models"][model] = {
        "average_score": round(sum(scores) / len(scores), 3),
        "wins": sum(r["winner"] == model for r in results),
        "scores": dict(Counter(scores)),
        "numeric_warnings": sum(
            bool(r.get(model + "_numeric_warnings")) for r in results
        ) if model != "baseline" else 0
    }
summary["ties"] = sum(r["winner"] == "tie" for r in results)
(root / "judge-only-20-summary.json").write_text(
    json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
)
print("JUDGE_ONLY_SUMMARY " + json.dumps(summary, ensure_ascii=False, separators=(",", ":")), flush=True)
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary_file:
        summary_file.write("## Blind review of SAVED translations (no retranslation)\n\n")
        summary_file.write("| Model | Avg score /5 | Wins | Numeric flags |\n|---|---:|---:|---:|\n")
        for model in models:
            s = summary["models"][model]
            summary_file.write(f"| {model} | {s['average_score']} | {s['wins']} | {s['numeric_warnings']} |\n")
        summary_file.write(f"\nTies: {summary['ties']}; reported/estimated review cost: \${spend:.5f}.\n"
                           "\nAll items remain machine-graded and unapproved by humans.\n")
