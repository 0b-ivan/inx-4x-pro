#!/usr/bin/env python3
"""Generate a human-review queue from 500 validated but UNAPPROVED drafts.

Offline classification only: no OpenRouter, no rewriting, no publication.
Historical AI 'reject' verdicts are never reversed by cleaner numeric parsing.
"""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

FIELDS=("q","a","alt","w")
PRIORITIES=("P0: numeric integrity","P1: prior AI rejection",
            "P1: wordplay/cultural clue","P2: AI rewrite review","P3: sample review")

def classify(row):
    if row.get("needs_human_review") is not True:
        raise ValueError("All 500 records must require human review")
    numeric=row.get("new_numeric_warnings") or []
    wordplay="potential_wordplay" in row.get("risk_flags",[])
    verdict=row.get("prior_ai_verdict")
    if numeric:
        return PRIORITIES[0],"Check number/unit equivalence and underlying facts; do not auto-clear"
    if verdict=="reject":
        return PRIORITIES[1],"Adjudicate earlier model rejection even if numeric parser now finds no mismatch"
    if wordplay:
        return PRIORITIES[2],"Verify idiomatic wordplay and unique intended answer with bilingual editor"
    if verdict=="rewrite":
        return PRIORITIES[3],"Compare original draft against AI's suggested wording before approval"
    return PRIORITIES[4],"Unreviewed translation: ordinary editorial spot-check candidate"

def compile_queue(rows):
    if len(rows)!=500 or len({r["id"] for r in rows})!=500:
        raise ValueError("Exactly 500 distinct entries required")
    queue=[]
    for r in rows:
        if any(k not in r.get("english",{}) or k not in r.get("german",{}) for k in FIELDS):
            raise ValueError("English/German records missing required fields")
        priority,action=classify(r)
        queue.append({"id":r["id"],"priority":priority,"review_action":action,
                      "english_question":r["english"]["q"],
                      "english_answer":r["english"]["a"],
                      "english_alternatives":json.dumps(r["english"]["alt"],ensure_ascii=False),
                      "english_distractors":json.dumps(r["english"]["w"],ensure_ascii=False),
                      "german_question":r["german"]["q"],
                      "german_answer":r["german"]["a"],
                      "german_alternatives":json.dumps(r["german"]["alt"],ensure_ascii=False),
                      "german_distractors":json.dumps(r["german"]["w"],ensure_ascii=False),
                      "numeric_warnings":json.dumps(r.get("new_numeric_warnings",[]),ensure_ascii=False),
                      "verified_unit_notes":json.dumps(r.get("unit_conversion_notes",[]),ensure_ascii=False),
                      "other_risks":json.dumps(r.get("risk_flags",[]),ensure_ascii=False),
                      "prior_ai_verdict":r.get("prior_ai_verdict") or "",
                      "prior_ai_reason":r.get("prior_ai_reason") or "",
                      "needs_human_review":True})
    rank={p:i for i,p in enumerate(PRIORITIES)}
    queue.sort(key=lambda q:(rank[q["priority"]],q["id"]))
    return queue

def main():
    parser=argparse.ArgumentParser()
    for name in ("input","csv","jsonl","summary","markdown"):
        parser.add_argument("--"+name,required=True)
    args=parser.parse_args()
    rows=[json.loads(s) for s in Path(args.input).read_text(encoding="utf-8").splitlines() if s.strip()]
    queue=compile_queue(rows)
    fields=list(queue[0])
    with Path(args.csv).open("w",encoding="utf-8",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=fields)
        writer.writeheader()
        writer.writerows(queue)
    with Path(args.jsonl).open("w",encoding="utf-8") as f:
        for r in queue:
            f.write(json.dumps(r,ensure_ascii=False,separators=(",",":"))+"\n")
    priorities=Counter(row["priority"] for row in queue)
    prior_rejects=sum(bool(r["prior_ai_verdict"]=="reject") for r in queue)
    prior_rewrites=sum(bool(r["prior_ai_verdict"]=="rewrite") for r in queue)
    numeric_warn=sum(bool(r["numeric_warnings"]!="[]") for r in queue)
    summary={
        "questions":500,
        "all_unapproved":True,
        "new_translation_calls":0,"new_review_calls":0,"api_cost_usd":0,
        "numeric_warning_records":numeric_warn,
        "previously_ai_rejected_needing_human_adjudication":prior_rejects,
        "previously_ai_rewritten_needing_human_adjudication":prior_rewrites,
        "by_priority":dict(priorities),
        "manual_review_required":500,
        "policy":"Never equate an automated risk-free verdict with human approval"
    }
    Path(args.summary).write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    lines=["# Review queue for 500 German trivia drafts","",
           "Offline human-review triage. No draft is approved or added to firmware.","",
           "| Priority | Records |","|---|---:|"]
    for priority in PRIORITIES:
        lines.append(f"| {priority} | {priorities[priority]} |")
    lines+=["",f"Historical AI rejections still requiring human adjudication: **{prior_rejects}**.",
            f"Numeric mismatches remaining: **{numeric_warn}**.","",
            "## Highest-priority examples","","| Source ID | English clue | German draft | Review reason |",
            "|---|---|---|---|"]
    def shorten(v):
        return str(v).replace("|","\\|").replace("\n"," ").replace("\r"," ")[:145]
    for row in [r for r in queue if r["priority"]!=PRIORITIES[-1]][:20]:
        lines.append("| {} | {} | {} | {} |".format(
            row["id"],shorten(row["english_question"]),shorten(row["german_question"]),
            shorten(row["review_action"])))
    lines+=["","**Next step:** Human editor reviews high-priority cases; preserve original",
            "source IDs, answer/distractor semantics and the required 448-byte/Quizmaster limits.",
            "Do not mark reviewed=true without human sign-off."]
    Path(args.markdown).write_text("\n".join(lines)+"\n",encoding="utf-8")
    print("QWEN_HUMAN_REVIEW_QUEUE "+json.dumps(summary,ensure_ascii=False,separators=(",",":")),flush=True)
    if __import__("os").environ.get("GITHUB_STEP_SUMMARY"):
        with open(__import__("os").environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as f:
            f.write("\n## Human review priorities\n\n")
            f.write("| Priority | Count |\n|---|---:|\n")
            for name in PRIORITIES:
                f.write(f"| {name} | {priorities[name]} |\n")
            f.write("\n**500 unapproved; 0 model calls; 0 USD spent.**\n")
if __name__=="__main__":
    main()
