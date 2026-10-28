#!/usr/bin/env python3
"""Merge converted CAW card sources into one deck, one card per text.

    python3 merge_cards.py --deck-id community-de --title "Community DE" \\
        --input prior99.json prior99.jsonl --input bixelpitch.json bixelpitch.jsonl \\
        --cards merged.jsonl --manifest merged.json --report merged-report.json

Inputs are (manifest, cards) pairs from the importers, earlier inputs win:
a duplicate keeps the first source's text and pick, and gains the other
sources' categories. Categories are matched by id; one is adult if any
source says so and on by default only if every source says so. The result
stays unverified and unreviewed; a single license is kept only when every
source has the same one.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

from card_text import normalized

MAX_CATEGORIES=16

class MergeError(ValueError):
    pass

def merge(inputs,deck_id,title):
    categories,order=[],{}
    for manifest,_ in inputs:
        for cat in manifest.get("categories",[]):
            if cat["id"] not in order:
                order[cat["id"]]=len(categories)
                categories.append(dict(cat))
            else:
                known=categories[order[cat["id"]]]
                known["adult"]=known["adult"] or cat["adult"]
                known["default_on"]=known["default_on"] and cat["default_on"]
    for cat in categories:
        if cat["adult"]:
            cat["default_on"]=False
    if len(categories)>MAX_CATEGORIES:
        raise MergeError(f"{len(categories)} categories, the pack format holds {MAX_CATEGORIES}")
    cards,seen,conflicts=[],{},[]
    per_source={}
    for manifest,rows in inputs:
        name=manifest["deck_id"]
        stats=per_source.setdefault(name,{"prompts":0,"answers":0,"shared":0})
        for row in rows:
            stats["prompts" if row["kind"]=="prompt" else "answers"]+=1
            key=(row["kind"],normalized(row["text"]))
            if key in seen:
                kept=seen[key]
                stats["shared"]+=1
                for cid in row["categories"]:
                    if cid not in kept["categories"]:
                        kept["categories"].append(cid)
                if name not in kept["sources"]:
                    kept["sources"].append(name)
                if row["kind"]=="prompt" and row["pick"]!=kept["pick"]:
                    conflicts.append({"text":kept["text"],"kept_pick":kept["pick"],
                                      "other":name,"other_pick":row["pick"]})
                continue
            merged={k:v for k,v in row.items() if k not in ("source","source_line")}
            merged["categories"]=list(row["categories"])
            merged["sources"]=[name]
            merged["reviewed"]=False
            seen[key]=merged
            cards.append(merged)
    licenses={manifest.get("license") for manifest,_ in inputs}
    license=licenses.pop() if len(licenses)==1 else None
    first=inputs[0][0]
    manifest={
        "deck_id":deck_id,"version":1,"source_locale":"de","title":title,
        "source_url":first.get("source_url"),
        "sources":[{"deck_id":m["deck_id"],"source_url":m.get("source_url"),
                    "source_commit":m.get("source_commit"),"license":m.get("license"),
                    "rights_holder":m.get("rights_holder")} for m,_ in inputs],
        "rights_holder":"; ".join(m.get("rights_holder","") for m,_ in inputs),
        "license":license,
        "license_url":first.get("license_url") if license else None,
        "rights_status":"unverified","editorial_status":"unreviewed",
        "categories":categories,
    }
    report={"per_source":per_source,
            "merged":{"prompts":sum(c["kind"]=="prompt" for c in cards),
                      "answers":sum(c["kind"]=="answer" for c in cards)},
            "pick_conflicts":conflicts}
    return cards,manifest,report

def load(manifest_path,cards_path):
    manifest=json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    rows=[json.loads(line) for line in Path(cards_path).read_text(encoding="utf-8").splitlines() if line.strip()]
    return manifest,rows

def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input",nargs=2,action="append",required=True,metavar=("MANIFEST","CARDS"))
    ap.add_argument("--deck-id",required=True)
    ap.add_argument("--title",required=True)
    ap.add_argument("--cards",type=Path,required=True)
    ap.add_argument("--manifest",type=Path,required=True)
    ap.add_argument("--report",type=Path,required=True)
    args=ap.parse_args()
    try:
        cards,manifest,report=merge([load(m,c) for m,c in args.input],args.deck_id,args.title)
    except (MergeError,OSError,KeyError,json.JSONDecodeError) as exc:
        ap.error(str(exc))
    args.cards.write_text("".join(json.dumps(c,ensure_ascii=False)+"\n" for c in cards),encoding="utf-8")
    args.manifest.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k!="pick_conflicts"},ensure_ascii=False))
    print(f"pick conflicts: {len(report['pick_conflicts'])}",file=sys.stderr)
    return 0

if __name__=="__main__":
    sys.exit(main())
