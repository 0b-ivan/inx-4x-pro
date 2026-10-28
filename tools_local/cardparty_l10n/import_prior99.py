#!/usr/bin/env python3
"""Convert Prior99/cah-fullformat-german (LaTeX) into CAW card JSONL.

Offline: reads a local clone, never downloads. Output cards are
reviewed=false and the manifest is unverified: build_sd_pack.py refuses a
release pack until a human changes both. Categories come from the source's
own category files; FSK 18 is the adult category and starts off.

    python3 import_prior99.py --source ./cah-fullformat-german \\
        --cards prior99.jsonl --manifest prior99.json --report prior99-report.json
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from card_text import BLANK, DRAW_FOR_PICK, normalized, tidy

SOURCE_URL="https://github.com/Prior99/cah-fullformat-german"
# Source file stem -> (category id, display name, adult). Order is the bit order.
CATEGORIES=[
    ("basis","basis","Basis",False),
    ("boese","boese","Böse",False),
    ("drogen","drogen","Drogen",False),
    ("eklig","eklig","Eklig",False),
    ("geschichte","geschichte","Geschichte",False),
    ("kultur","kultur","Kultur",False),
    ("fsk16","fsk-16","FSK 16",False),
    ("fsk18","fsk-18","FSK 18",True),
    ("politik","politik","Politik",False),
    ("religion","religion","Religion",False),
    ("werbung","werbung","Werbung",False),
]

class ImportError_(ValueError):
    pass

def braced(text,start):
    """Return (content, index after the closing brace) for text[start]=='{'."""
    if start>=len(text) or text[start]!="{":
        raise ImportError_("expected {")
    depth=0
    for i in range(start,len(text)):
        if text[i]=="{":
            depth+=1
        elif text[i]=="}":
            depth-=1
            if depth==0:
                return text[start+1:i],i+1
    raise ImportError_("unbalanced braces")

def clean(raw):
    """LaTeX card text -> plain UTF-8 card text."""
    text=raw.replace(r"\underl",f" {BLANK} ").replace(r"\newline"," ")
    text=text.replace(r"\SymbReg","®").replace(r"\%","%").replace(r"\&","&")
    # LaTeX quotes: `` opens, '' closes; the source also closes with ``.
    out,open_quote,i=[],False,0
    while i<len(text):
        pair=text[i:i+2]
        if pair=="``":
            out.append("“" if open_quote else "„");open_quote=not open_quote;i+=2
        elif pair=="''":
            out.append("“");open_quote=False;i+=2
        else:
            out.append(text[i]);i+=1
    text="".join(out)
    if "\\" in text or "{" in text or "}" in text:
        raise ImportError_(f"unconverted LaTeX in {raw!r}")
    # Print hyphenation hints: "Sonnen- untergang" -> "Sonnenuntergang",
    # "Volleyball- Montage" -> "Volleyball-Montage".
    text=re.sub(r"(\w)- (?=[a-zäöüß])",r"\1",text)
    text=re.sub(r"(\w)- (?=\w)",r"\1-",text)
    return tidy(text)

def card_id(kind,category,text):
    digest=hashlib.sha256(f"{kind}\0{category}\0{text}".encode()).hexdigest()[:10]
    return f"prior99-{kind[0]}-{category}-{digest}"

def parse_file(path,category,issues):
    cards=[]
    for number,line in enumerate(path.read_text(encoding="utf-8").splitlines(),1):
        line=line.strip()
        if not line or line.startswith("%") or line.startswith(r"\titlecard"):
            continue
        where=f"{path.name}:{number}"
        try:
            if line.startswith(r"\whitecard{"):
                _,at=braced(line,len(r"\whitecard"))
                raw,_=braced(line,at)
                text=clean(raw)
                if BLANK in text:
                    raise ImportError_("answer contains a blank")
                cards.append({"kind":"answer","text":text,"where":where})
            elif line.startswith(r"\blackcard{"):
                count,at=braced(line,len(r"\blackcard"))
                _,at=braced(line,at)
                raw,_=braced(line,at)
                pick=int(count)
                if pick not in DRAW_FOR_PICK:
                    raise ImportError_(f"pick {pick}")
                text=clean(raw)
                blanks=text.count(BLANK)
                if blanks not in (0,pick):
                    raise ImportError_(f"{blanks} blanks for pick {pick}")
                cards.append({"kind":"prompt","text":text,"pick":pick,
                              "draw":DRAW_FOR_PICK[pick],"where":where})
            elif "card{" in line:
                raise ImportError_("unknown card macro")
        except (ImportError_,ValueError) as exc:
            issues.append({"where":where,"problem":str(exc),"line":line})
    for card in cards:
        card["category"]=category
    return cards

def convert(source):
    source=Path(source)
    issues,cards=[],[]
    for stem,cid,_,_ in CATEGORIES:
        path=source/"categories"/f"{stem}.tex"
        if not path.is_file():
            raise ImportError_(f"missing {path}")
        cards.extend(parse_file(path,cid,issues))
    seen,out,duplicates={},[],[]
    for card in cards:
        key=(card["kind"],normalized(card["text"]))
        if key in seen:
            first=seen[key]
            if card["category"] not in first["categories"]:
                first["categories"].append(card["category"])
            duplicates.append({"kept":first["source_line"],"dropped":card["where"],"text":card["text"]})
            continue
        row={"id":card_id(card["kind"],card["category"],card["text"]),"kind":card["kind"],
             "text":card["text"],"categories":[card["category"]],"reviewed":False,
             "source":"prior99","source_line":card["where"]}
        if card["kind"]=="prompt":
            row["pick"],row["draw"]=card["pick"],card["draw"]
        seen[key]=row
        out.append(row)
    return out,issues,duplicates

def manifest(commit):
    return {
        "deck_id":"prior99-de","version":1,"source_locale":"de",
        "title":"CAH Fullformat German (Prior99)",
        "source_url":SOURCE_URL,"source_commit":commit,
        "rights_holder":"Prior99 and contributors; based on Cards Against Humanity",
        # Unresolved: the repo says GPL-3.0, the CAH originals are
        # CC BY-NC-SA 2.0. A human decides before release.
        "license":None,"license_url":None,
        "rights_status":"unverified","editorial_status":"unreviewed",
        "categories":[{"id":cid,"name":name,"default_on":not adult,"adult":adult}
                      for _,cid,name,adult in CATEGORIES],
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
    except (ImportError_,OSError,UnicodeError) as exc:
        ap.error(str(exc))
    args.cards.write_text("".join(json.dumps(c,ensure_ascii=False)+"\n" for c in cards),encoding="utf-8")
    args.manifest.write_text(json.dumps(manifest(args.commit),ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    counts={cid:{"prompts":0,"answers":0} for _,cid,_,_ in CATEGORIES}
    for card in cards:
        for cid in card["categories"]:
            counts[cid]["prompts" if card["kind"]=="prompt" else "answers"]+=1
    report={"prompts":sum(c["kind"]=="prompt" for c in cards),
            "answers":sum(c["kind"]=="answer" for c in cards),
            "without_blank":sum(c["kind"]=="prompt" and BLANK not in c["text"] for c in cards),
            "per_category":counts,"duplicates":duplicates,"issues":issues}
    args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k not in ("duplicates","issues")},ensure_ascii=False))
    print(f"duplicates merged: {len(duplicates)}, issues: {len(issues)}",file=sys.stderr)
    return 1 if issues else 0

if __name__=="__main__":
    sys.exit(main())
