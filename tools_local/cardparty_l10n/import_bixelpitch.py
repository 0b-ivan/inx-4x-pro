#!/usr/bin/env python3
"""Convert BixelPitch/cah-cards (German Grundspiel, JSON) into CAW card JSONL.

Offline: reads a local clone, never downloads. The repository states
CC BY-NC-SA 2.0; the manifest records that but stays unverified and every
card reviewed=false, so build_sd_pack.py only builds a draft until a human
releases it. The source has no content categories: every card is "Basis".

    python3 import_bixelpitch.py --source ./cah-cards \\
        --cards bixelpitch.jsonl --manifest bixelpitch.json --report bixelpitch-report.json
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

from card_text import BLANK, DRAW_FOR_PICK, normalized, tidy

SOURCE_URL="https://github.com/BixelPitch/cah-cards"
PACK="dist/de/grundspiel.json"
CATEGORY=("basis","Basis")

class ImportError_(ValueError):
    pass

def clean(raw):
    if not isinstance(raw,str):
        raise ImportError_("card is not a string")
    # Some cards carry a literal "\n" (backslash n) as a line break.
    return tidy(raw.replace("\\n"," ").replace("\n"," "))

def card_id(kind,text):
    digest=hashlib.sha256(f"{kind}\0{text}".encode()).hexdigest()[:10]
    return f"bixelpitch-{kind[0]}-{digest}"

def convert(source):
    path=Path(source)/PACK
    data=json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data,dict) or data.get("language")!="de":
        raise ImportError_(f"{PACK} is not the German pack")
    rows,issues,duplicates,seen=[],[],[],{}
    for kind,key in (("prompt","black_cards"),("answer","white_cards")):
        cards=data.get(key)
        if not isinstance(cards,list):
            raise ImportError_(f"{key} missing")
        for index,raw in enumerate(cards):
            where=f"{PACK}:{key}[{index}]"
            try:
                text=clean(raw)
                blanks=text.count(BLANK)
                if kind=="answer" and blanks:
                    raise ImportError_("answer contains a blank")
                if kind=="prompt" and blanks>3:
                    raise ImportError_(f"{blanks} blanks")
            except ImportError_ as exc:
                issues.append({"where":where,"problem":str(exc),"text":raw})
                continue
            dedup=(kind,normalized(text))
            if dedup in seen:
                duplicates.append({"kept":seen[dedup]["source_line"],"dropped":where,"text":text})
                continue
            row={"id":card_id(kind,text),"kind":kind,"text":text,"categories":[CATEGORY[0]],
                 "reviewed":False,"source":"bixelpitch","source_line":where}
            if kind=="prompt":
                # The source has no pick field: a prompt asks for one card per
                # blank, and a question without a blank for one card.
                pick=max(1,blanks)
                row["pick"],row["draw"]=pick,DRAW_FOR_PICK[pick]
            seen[dedup]=row
            rows.append(row)
    return rows,issues,duplicates

def manifest(commit):
    return {
        "deck_id":"bixelpitch-de","version":1,"source_locale":"de",
        "title":"CAH Grundspiel (BixelPitch)",
        "source_url":SOURCE_URL,"source_commit":commit,
        "rights_holder":"BixelPitch; based on Cards Against Humanity",
        "license":"CC-BY-NC-SA-2.0",
        "license_url":"https://creativecommons.org/licenses/by-nc-sa/2.0/",
        "rights_status":"unverified","editorial_status":"unreviewed",
        "categories":[{"id":CATEGORY[0],"name":CATEGORY[1],"default_on":True,"adult":False}],
    }

def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source",type=Path,required=True,help="local clone of the repository")
    ap.add_argument("--cards",type=Path,required=True)
    ap.add_argument("--manifest",type=Path,required=True)
    ap.add_argument("--report",type=Path,required=True)
    ap.add_argument("--commit",default="",help="source commit, recorded in the manifest")
    args=ap.parse_args()
    try:
        cards,issues,duplicates=convert(args.source)
    except (ImportError_,OSError,UnicodeError,json.JSONDecodeError) as exc:
        ap.error(str(exc))
    args.cards.write_text("".join(json.dumps(c,ensure_ascii=False)+"\n" for c in cards),encoding="utf-8")
    args.manifest.write_text(json.dumps(manifest(args.commit),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    report={"prompts":sum(c["kind"]=="prompt" for c in cards),
            "answers":sum(c["kind"]=="answer" for c in cards),
            "without_blank":sum(c["kind"]=="prompt" and BLANK not in c["text"] for c in cards),
            "duplicates":duplicates,"issues":issues}
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k not in ("duplicates","issues")},ensure_ascii=False))
    print(f"duplicates merged: {len(duplicates)}, issues: {len(issues)}",file=sys.stderr)
    return 1 if issues else 0

if __name__=="__main__":
    sys.exit(main())
