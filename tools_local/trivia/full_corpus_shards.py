#!/usr/bin/env python3
"""Budgeted first pass of 49,958 source questions across 50 independent shards.

The user authorized 500 existing editorial drafts in bulk; the remaining
translations are NOT human-approved. Quarantine display-impossible records.
No deployment or firmware release is possible from this script.
"""
import argparse,json,os,time,urllib.error
from pathlib import Path
from build_de_pack import load_jsonl,source_hash,validate_record,TranslationError
from translate_de_pack import translate_one
SHARDS=50
FIELDS=("q","a","alt","w")
MODEL="qwen/qwen3-235b-a22b-2507"
def load(p): return list(load_jsonl(p)) if Path(p).exists() else []
def save_line(path,row):
    with open(path,"a",encoding="utf-8") as f:
        f.write(json.dumps(row,ensure_ascii=False,separators=(",",":"))+"\n")
        f.flush()
def originals(p):
    rows=load(p)
    ids=[r["id"] for r in rows]
    if len(ids)!=49958 or len(set(ids))!=49958:
        raise ValueError("Original pack does not contain 49,958 unique source IDs")
    return rows
def verify_seed(src,seed,approved):
    byid={r["id"]:r for r in src}
    if len(seed)!=500 or len({r["id"] for r in seed})!=500:
        raise ValueError("Seed must contain exactly 500 distinct source IDs")
    for row in seed:
        original=byid.get(row["id"])
        if not original or row.get("source_hash")!=source_hash(original):
            raise ValueError("Seed doesn't match pinned English source")
        if approved and (row.get("reviewed") is not True or
            row.get("review_origin")!="user_bulk_authorization_2026-10-26"):
            raise ValueError("Missing user's explicit bulk approval")
        if not approved and row.get("reviewed") is not False:
            raise ValueError("Original proposal already marked reviewed")
        validate_record(original,row,allow_draft=not approved)
    return {r["id"]:r for r in seed}
def prepare(args):
    src=originals(args.source)
    seed=verify_seed(src,load(args.proposals),False)
    byid={r["id"]:r for r in src}
    if Path(args.out).exists(): raise ValueError("Refusing to overwrite existing approval export")
    for draft in seed.values():
        row={**draft,"reviewed":True,"review_origin":"user_bulk_authorization_2026-10-26"}
        validate_record(byid[row["id"]],row)
        save_line(args.out,row)
    print("BULK_APPROVAL 500 prior edited drafts; no firmware release",flush=True)
def translate(args):
    src=originals(args.source)
    seed=verify_seed(src,load(args.seed),True)
    positions=list(range(args.shard,len(src),SHARDS))
    expected={src[i]["id"] for i in positions}
    cached=load(args.out)
    done={r["id"]:r for r in cached}
    byid={r["id"]:r for r in src}
    for row in cached:
        validate_record(byid[row["id"]],row,allow_draft=True)
    quarantines=load(args.quarantine)
    failures={r["id"] for r in quarantines}
    for row in quarantines:
        if row["id"] not in byid:
            raise ValueError("Unknown quarantine checkpoint ID")
        if row.get("source_hash")!=source_hash(byid[row["id"]]):
            raise ValueError("Quarantine checkpoint source hash mismatch")
    if len(done)!=len(cached) or len(failures)!=len(quarantines) or (set(done)&failures):
        raise ValueError("Duplicated or overlapping checkpoint rows")
    if not (set(done)|failures)<=expected:
        raise ValueError("Checkpoint contains source IDs outside this shard")
    key=os.getenv("OPENROUTER_API_KEY")
    if not key: raise ValueError("OpenRouter key missing")
    for i in positions:
        original=src[i]
        ident=original["id"]
        if ident in done or ident in failures:
            continue
        if ident in seed:
            row=seed[ident]
            validate_record(original,row)
            save_line(args.out,row)
            done[ident]=row
            continue
        errors=[]
        accepted=None
        for attempt in range(2):
            instruction=("" if attempt==0 else
                "HARD DISPLAY RETRY: Previous German result exceeded Quizmaster "
                "pixel width or 448-byte binary record. Use extremely concise "
                "but factually faithful German and shorter proper name forms. "
                "Preserve the original correct answer and all alternative "
                "and incorrect answer choices in original order. Never invent facts.")
            try:
                translation=translate_one(original,MODEL,key,timeout=95,extra_instruction=instruction)
                candidate={"id":ident,"source_hash":source_hash(original),
                           "reviewed":False,**{k:translation[k] for k in FIELDS}}
                validate_record(original,candidate,allow_draft=True)
                accepted=candidate
                break
            except (ValueError,KeyError,TranslationError) as err:
                if "spending guard" in str(err).lower():
                    raise
                errors.append(str(err)[:200])
            except urllib.error.HTTPError as err:
                if err.code in (401,402,403,404):
                    raise RuntimeError("OpenRouter blocked HTTP "+str(err.code)+"; check model/provider availability and request parameters")
                if err.code==429 or err.code>=500:
                    if attempt==1: raise
                    time.sleep(9)
                else: raise
            except (urllib.error.URLError,TimeoutError):
                if attempt==1: raise
                time.sleep(9)
        if accepted:
            save_line(args.out,accepted)
            done[ident]=accepted
        else:
            save_line(args.quarantine,{"id":ident,"source_hash":source_hash(original),
                "reason":errors[-1] if errors else "no fitting German translation",
                "needs_human_review":True})
            failures.add(ident)
        if (len(done)+len(failures))%100==0:
            print("SHARD_PROGRESS id={} done={} of={} failed={}".format(
                args.shard,len(done)+len(failures),len(positions),len(failures)),flush=True)
    if set(done)|failures != expected or set(done)&failures:
        raise ValueError("Incomplete or overlapping shard checkpoint")
    usage=load(args.ledger)
    summary={"shard":args.shard,"total":len(positions),"translated":len(done),
             "quarantined":len(failures),"calls":len(usage),
             "cost_usd":round(sum(float(x["cost_usd"]) for x in usage),6)}
    Path(args.summary).write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print("SHARD_SUMMARY "+json.dumps(summary),flush=True)
def assemble(args):
    source=originals(args.source)
    byid={r["id"]:r for r in source}
    done={}
    problems={}
    missing_shards=[]
    costs=0.0; calls=0
    for n in range(SHARDS):
        folder=Path(args.shards)/("shard-%02d"%n)
        translated=folder/("questions-%02d.jsonl"%n)
        quarantined=folder/("quarantine-%02d.jsonl"%n)
        ledger=folder/("usage-%02d.jsonl"%n)
        if not translated.is_file():
            missing_shards.append(n)
            continue
        for row in load(translated):
            ident=row["id"]
            if ident not in byid or ident in done:
                raise ValueError("Duplicate/unknown translated identifier "+ident)
            validate_record(byid[ident],row,allow_draft=True)
            done[ident]=row
        for row in load(quarantined):
            ident=row["id"]
            if ident not in byid or ident in problems:
                raise ValueError("Duplicate/unknown quarantined identifier "+ident)
            problems[ident]=row
        rows=load(ledger)
        calls+=len(rows)
        costs+=sum(float(x["cost_usd"]) for x in rows)
    if set(done)&set(problems):
        raise ValueError("Questions both translated and quarantined")
    with open(args.out,"w",encoding="utf-8") as f:
        for item in source:
            if item["id"] in done:
                f.write(json.dumps(done[item["id"]],ensure_ascii=False,separators=(",",":"))+"\n")
    with open(args.quarantine,"w",encoding="utf-8") as f:
        for item in problems.values():
            f.write(json.dumps(item,ensure_ascii=False)+"\n")
    unprocessed=sum(1 for item in source if item["id"] not in done and item["id"] not in problems)
    result={"english_total":len(source),"german_valid":len(done),
            "quarantined":len(problems),"unprocessed":unprocessed,
            "missing_shards":missing_shards,"paid_requests":calls,
            "new_api_cost_usd":round(costs,6),
            "firmware_released":False,"human_review_required":True}
    Path(args.summary).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("FULL_CORPUS_FIRST_PASS "+json.dumps(result,ensure_ascii=False),flush=True)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as f:
            f.write("## German trivia full corpus – first pass\n\n")
            f.write("| Measure | Value |\n|---|---:|\n")
            for k,v in result.items(): f.write("|{}|{}|\n".format(k,v))
            f.write("\nNot a firmware release. Quarantined records remain unresolved.\n")
def main():
    p=argparse.ArgumentParser()
    sub=p.add_subparsers(dest="cmd",required=True)
    a=sub.add_parser("prepare")
    for k in ("source","proposals","out"): a.add_argument("--"+k,required=True)
    b=sub.add_parser("translate")
    for k in ("source","seed","out","quarantine","ledger","summary"): b.add_argument("--"+k,required=True)
    b.add_argument("--shard",type=int,choices=range(SHARDS),required=True)
    c=sub.add_parser("assemble")
    for k in ("source","shards","out","quarantine","summary"): c.add_argument("--"+k,required=True)
    opts=p.parse_args()
    {"prepare":prepare,"translate":translate,"assemble":assemble}[opts.cmd](opts)
if __name__=="__main__":
    main()
