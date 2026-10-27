#!/usr/bin/env python3
"""Deterministic German party-card editorial LINT, never human approval.

Runs after the cryptographic/source schema validation. Static grammar heuristics
cannot prove that *arbitrary* combinations are funny or grammatically correct.
"""
import argparse
import json
from pathlib import Path
import re

from translate_cards import CardError, load_drafts, load_source

_CASE_TRAPS = re.compile(r"\b(?:wegen|durch|beinhaltet|hat|haben|ersetzt)\s+____\b", re.I)
_END_SENTENCE = re.compile(r"[.!?]\s*$")
_TAG = re.compile(r"\[\d+\]\s+____")


def lint_card(source, translated):
    ident = source["id"]
    target = translated["text"].strip()
    issues = []
    if source["kind"] == "answer":
        if _END_SENTENCE.search(target):
            issues.append({
                "id": ident, "severity": "error",
                "code": "answer_terminal_punctuation",
                "detail": "Standalone response may be followed by another selected response; terminal punctuation causes malformed multi-pick combinations.",
            })
        if source["text"].lower().startswith("an alarm ") and re.search(r"\bwecker\b", target, re.I):
            issues.append({
                "id": ident, "severity": "error", "code": "alarm_not_clock",
                "detail": "English 'alarm' refers to an alarm signal, not necessarily a physical alarm clock.",
            })
    else:
        if _CASE_TRAPS.search(target):
            issues.append({
                "id": ident, "severity": "error", "code": "case_governing_blank",
                "detail": "Verb/preposition adjacent to blank may require inflection the nominated answer-card forms do not provide.",
            })
        # If a prompt literally ends with '. / ? / !' after a substitution
        # marker, joining a complete answer with punctuation also reads badly.
        if re.search(r"____[.!?]\s*$", target):
            issues.append({
                "id": ident, "severity": "warning", "code": "punctuation_after_blank",
                "detail": "Sentence-final punctuation immediately after a blank may collide with answer punctuation.",
            })
        if source["pick"] > 1 and len(_TAG.findall(target)) < source["pick"]:
            issues.append({
                "id": ident, "severity": "warning", "code": "multi_pick_composition",
                "detail": "Consider separately labeled standalone selection slots to avoid case/conjunction mismatch.",
            })
    return issues


def audit(manifest, source, drafts):
    output = []
    for ident, source_card in source.items():
        if ident not in drafts:
            output.append({"id": ident, "severity": "error", "code": "missing_draft",
                           "detail": "No translation proposal exists."})
        else:
            output.extend(lint_card(source_card, drafts[ident]))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--drafts", required=True)
    parser.add_argument("--json-report", help="Optional machine-readable editorial findings")
    args = parser.parse_args()
    try:
        manifest, source = load_source(args.manifest, args.source)
        drafts = load_drafts(args.drafts, manifest, source)
        findings = audit(manifest, source, drafts)
        report = {"total_cards": len(source), "checked": len(drafts),
                  "findings": findings, "human_review_required": True,
                  "release_approved": False}
        if args.json_report:
            Path(args.json_report).write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
        for item in findings:
            print(f"{item['severity'].upper()} {item['id']} [{item['code']}] {item['detail']}")
        print(f"EDITORIAL: {len(drafts)}/{len(source)} structurally valid, "
              f"{len(findings)} heuristic findings; HUMAN REVIEW still required.")
        if any(x["severity"] == "error" for x in findings):
            raise CardError("Editorial lint blocked: fix errors before requesting human approval")
    except (CardError, OSError, ValueError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
