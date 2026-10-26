#!/usr/bin/env python3
"""Seed and audit a bounded 500-item Qwen German trivia experiment.

Never publishes a binary pack or marks a question human-reviewed.
"""
import argparse
import hashlib
import json
import os
import re
from collections import Counter
from pathlib import Path

from build_de_pack import source_hash, validate_record
from numerical_equivalence import numeric_warnings
from review_de_pack import apply_review, review_one

FIELDS=("q","a","alt","w")
WORDPLAY=re.compile(
    r"\b(?:rhyme|rhymes|rhyming|pun|puns|wordplay|anagram|spelled|"
    r"spelling|letters|sounds like|homonym|antonym|synonym|double meaning|"
    r"idiom|slang|nickname|word means|word for|word meaning|acronym)\b",re.I)

def load(path):
    return [json.loads(line) for line in Path(path).read_text(encoding="utf-8").splitlines() if line.strip()]

def write_jsonl(path,records):
    with Path(path).open("w",encoding="utf-8") as f:
        for row in records:
            f.write(json.dumps(row,ensure_ascii=False,separators=(",",":"))+"\n")

def original_index(path):
    return {row["id"]:row for row in load(path)}

def check_source(original,draft):
    if draft.get("source_hash")!=source_hash(original) or draft.get("reviewed") is not False:
        raise ValueError("Stale source or non-draft approval flag")
    validate_record(original,draft,allow_draft=True)

def ledger(path):
    records=load(path) if Path(path).exists() else []
    return {"calls":len(records),"reported_or_estimated_usd":round(sum(float(x["cost_usd"]) for x in records),6),
            "estimated_calls":sum(bool(x.get("is_estimate")) for x in records),
            "input_tokens":sum(x.get("input_tokens",0) for x in records),
            "output_tokens":sum(x.get("output_tokens",0) for x in records)}

def seed(src,old,output):
    originals=original_index(src)
    rows=load(old)
    if len(rows)!=20 or len({r["id"] for r in rows})!=20:
        raise ValueError("Expected exactly 20 unique previously translated Qwen items")
    result=[]
    for r in rows:
        original=originals[r["id"]]
        if any(r["english"][key]!=original[key] for key in FIELDS):
            raise ValueError("Previous model result is not from the pinned English pack")
        content=r["translations"]["qwen"]
        if not isinstance(content,dict) or set(content)!=set(FIELDS):
            raise ValueError("Missing Qwen draft")
        row={"id":original["id"],"source_hash":source_hash(original),
             "reviewed":False,**{key:content[key] for key in FIELDS}}
        check_source(original,row)
        result.append(row)
    write_jsonl(output,result)
    print("QWEN_SEED 20 source-verified previous translations; no new API calls",flush=True)

def audit(src,drafts,edited,report,summary,trans_usage,review_usage):
    originals=original_index(src)
    rows=load(drafts)
    if len(rows)!=500 or len({r["id"] for r in rows})!=500:
        raise ValueError("500 unique validated Qwen drafts required before editorial review")
    flags={}
    for row in rows:
        original=originals[row["id"]]
        check_source(original,row)
        warnings=numeric_warnings(original,row)
        risk=[]
        if warnings: risk.append("numeric_difference")
        if WORDPLAY.search(original["q"]): risk.append("potential_wordplay")
        flags[row["id"]]={"risk":risk,"numeric_warnings":warnings}
    flagged=[row["id"] for row in rows if flags[row["id"]]["risk"]]
    # Spread the small editorial sample; record excess risks explicitly.
    stable=lambda ident:hashlib.sha256(("qwen500-audit-"+ident).encode()).hexdigest()
    flagged.sort(key=stable)
    selected_flags=flagged[:40]
    ordinary=[row["id"] for row in rows if not flags[row["id"]]["risk"]]
    ordinary.sort(key=stable)
    control=ordinary[:10]
    selected=set(selected_flags+control)
    decisions={}
    api_key=os.environ.get("OPENROUTER_API_KEY")
    if selected and not api_key:
        raise ValueError("Missing OpenRouter secret before independent review")
    for i,row in enumerate(rows):
        ident=row["id"]
        if ident not in selected: continue
        original=originals[ident]
        decision=review_one(original,row,"openai/gpt-5.4",api_key)
        corrected,record=apply_review(original,row,decision)
        check_source(original,corrected)
        decisions[ident]=(corrected,record)
        print("QWEN_EDITORIAL {}/{} id={} verdict={} applied={}".format(
            len(decisions),len(selected),ident,record["verdict"],record["applied"]),flush=True)
    amended=[]
    audit_rows=[]
    counts=Counter()
    for row in rows:
        ident=row["id"]
        update,report_one=decisions.get(ident,(row,None))
        amended.append(update)
        why=flags[ident]
        bucket=("risk_reviewed" if ident in selected_flags else
                "control_reviewed" if ident in control else
                "risk_unreviewed" if why["risk"] else "not_selected")
        counts[bucket]+=1
        if report_one: counts[report_one["verdict"]]+=1
        audit_rows.append({
            "id":ident,"scope":bucket,"flags":why["risk"],
            "numeric_warnings":why["numeric_warnings"],
            "ai_verdict":report_one["verdict"] if report_one else None,
            "ai_reason":report_one["reason"] if report_one else None,
            "ai_rewrite_applied":report_one["applied"] if report_one else False,
            "needs_human_review":True})
    write_jsonl(edited,amended)
    write_jsonl(report,audit_rows)
    result={
        "questions":500,"qwen_translations_reused":20,"new_qwen_translations":480,
        "unapproved":500,
        "flagged":len(flagged),"flagged_not_ai_reviewed":max(0,len(flagged)-len(selected_flags)),
        "risk_categories":dict(Counter(reason for d in flags.values() for reason in d["risk"])),
        "editorial_ai":dict(counts),
        "qwen_usage_new":ledger(trans_usage),
        "strong_editor_usage":ledger(review_usage),
        "note":"Automatic technical/editorial QA only; every row still requires human review"}
    Path(summary).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("QWEN_500_SUMMARY "+json.dumps(result,ensure_ascii=False,separators=(",",":")),flush=True)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as f:
            f.write("## German trivia: 500 Qwen questions\n\n")
            f.write("500 stored, all unapproved; 20 reused and 480 newly translated.\n\n")
            f.write("| Measure | Result |\n|---|---:|\n")
            for name,value in (("Risk flags",len(flagged)),("Flagged not AI reviewed",
                   max(0,len(flagged)-len(selected_flags))),
                   ("GPT-5.4 reviews",len(selected)),("Machine accepted",counts["accept"]),
                   ("Machine rewritten",counts["rewrite"]),("Machine rejected",counts["reject"])):
                f.write("| {} | {} |\n".format(name,value))
            f.write("\nQwen additional cost reported/estimated: {:.6f} USD\n\n".format(
                result["qwen_usage_new"]["reported_or_estimated_usd"]))
            f.write("Selective AI review cost reported/estimated: {:.6f} USD\n\n".format(
                result["strong_editor_usage"]["reported_or_estimated_usd"]))
            f.write("No human approvals or firmware publishing.\n")

def main():
    p=argparse.ArgumentParser()
    sp=p.add_subparsers(dest="command",required=True)
    a=sp.add_parser("seed")
    a.add_argument("--source",required=True)
    a.add_argument("--comparison",required=True)
    a.add_argument("--output",required=True)
    b=sp.add_parser("audit")
    b.add_argument("--source",required=True)
    b.add_argument("--drafts",required=True)
    b.add_argument("--edited",required=True)
    b.add_argument("--report",required=True)
    b.add_argument("--summary",required=True)
    b.add_argument("--translation-ledger",required=True)
    b.add_argument("--review-ledger",required=True)
    args=p.parse_args()
    if args.command=="seed":
        seed(args.source,args.comparison,args.output)
    else:
        audit(args.source,args.drafts,args.edited,args.report,args.summary,
              args.translation_ledger,args.review_ledger)
if __name__=="__main__":
    main()
