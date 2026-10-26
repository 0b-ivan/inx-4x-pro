#!/usr/bin/env python3
"""Re-analyse 500 saved Qwen drafts, without ANY model/API calls."""
import argparse
import json
from collections import Counter
from pathlib import Path
from build_de_pack import source_hash,validate_record
from numeric_semantics import audit_record
from qwen_500_experiment import WORDPLAY

def lines(path):
    return [json.loads(x) for x in Path(path).read_text(encoding="utf-8").splitlines() if x.strip()]
def main():
    p=argparse.ArgumentParser()
    for arg in ("source","drafts","prior-audit","report","summary"):
        p.add_argument("--"+arg,required=True)
    a=p.parse_args()
    src={x["id"]:x for x in lines(a.source)}
    drafts=lines(a.drafts)
    old=lines(a.prior_audit)
    if len(drafts)!=500 or len(old)!=500:
        raise SystemExit("Need 500 saved drafts and 500 prior audits")
    if len({x["id"] for x in drafts})!=500 or {x["id"] for x in drafts}!={x["id"] for x in old}:
        raise SystemExit("Missing or duplicate prior draft/audit IDs")
    old_by_id={x["id"]:x for x in old}
    report=[]
    counts=Counter()
    for draft in drafts:
        original=src.get(draft["id"])
        if not original or draft.get("source_hash")!=source_hash(original) or draft.get("reviewed") is not False:
            raise SystemExit("Stale source or unapproved flag tampering: "+draft["id"])
        validate_record(original,draft,allow_draft=True)
        new=audit_record(original,draft)
        prior=old_by_id[draft["id"]]
        was_numeric=bool(prior.get("numeric_warnings"))
        remains_numeric=bool(new["warnings"])
        is_wordplay=bool(WORDPLAY.search(original["q"]))
        flags=(["numeric_mismatch"] if remains_numeric else [])+(["potential_wordplay"] if is_wordplay else [])
        if was_numeric and not remains_numeric:
            counts["resolved_numeric_flags"]+=1
        if was_numeric and remains_numeric:
            counts["persistent_numeric_flags"]+=1
        if not was_numeric and remains_numeric:
            counts["new_numeric_flags"]+=1
        if new["notes"]:
            counts["verified_unit_conversion_records"]+=1
        if flags:
            counts["flagged_records"]+=1
        if prior.get("ai_verdict")=="reject" and not remains_numeric:
            counts["previously_rejected_now_numeric_clean"]+=1
        counts["count"]+=1
        report.append({
            "id":draft["id"],
            "english":{k:original[k] for k in ("q","a","alt","w")},
            "german":{k:draft[k] for k in ("q","a","alt","w")},
            "old_numeric_flags":prior.get("numeric_warnings",[]),
            "new_numeric_warnings":new["warnings"],
            "unit_conversion_notes":new["notes"],
            "risk_flags":flags,
            "prior_ai_verdict":prior.get("ai_verdict"),
            "prior_ai_reason":prior.get("ai_reason"),
            "needs_human_review":True
        })
    with Path(a.report).open("w",encoding="utf-8") as f:
        for row in report:
            f.write(json.dumps(row,ensure_ascii=False,separators=(",",":"))+"\n")
    result={"total_drafts":500,
            "old_numeric_flagged":sum(bool(x.get("numeric_warnings")) for x in old),
            "new_numeric_flagged":sum(bool(x["new_numeric_warnings"]) for x in report),
            "prior_ai_rejected":sum(x.get("ai_verdict")=="reject" for x in old),
            "prior_ai_rewrites":sum(x.get("ai_verdict")=="rewrite" for x in old),
            "remaining_risk_ids":[x["id"] for x in report if x["risk_flags"]],
            "counts":dict(counts),
            "api_calls":0,"api_cost_usd":0,
            "all_unapproved":True,
            "note":"No new translation or model review; numeric triage only. Prior editorial rejections are not reversed."}
    Path(a.summary).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("QWEN_NUMERIC_REAUDIT "+json.dumps({k:v for k,v in result.items() if k!="remaining_risk_ids"},ensure_ascii=False),flush=True)
    if __import__("os").getenv("GITHUB_STEP_SUMMARY"):
        with open(__import__("os").environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as f:
            f.write("## 500-question German trivia numeric re-audit (offline)\n\n")
            f.write("| Result | Count |\n|---|---:|\n")
            for name,key in (("Old numeric warnings","old_numeric_flagged"),
                             ("New numeric warnings","new_numeric_flagged"),
                             ("Resolved old warnings","counts")):
                val=result[key] if key!="counts" else counts["resolved_numeric_flags"]
                f.write(f"| {name} | {val} |\n")
            f.write("\nNo API requests, no translation, no human approvals, no release.\n")
if __name__=="__main__":
    main()
