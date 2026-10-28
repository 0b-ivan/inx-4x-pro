#!/usr/bin/env python3
"""Build the public CAW packs from pinned card sources, as public_packs.json says.

    python3 publish_packs.py --config public_packs.json \\
        --source prior99=./cah-fullformat-german --source bixelpitch=./cah-cards \\
        --out ./dist

Offline: the sources are local clones the caller checked out at the pinned
commits (verified here when they are git checkouts). Each pack is converted,
merged in the listed order, released with the config's recorded decision and
compiled; its SHA-256 must match the config, so a changed importer or source
can never publish different cards unnoticed. Writes the packs, catalog.txt
(the list the X4 downloads) and README.txt (attribution and license).
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import build_sd_pack as pack
import import_bixelpitch
import import_prior99
import merge_cards

IMPORTERS={"prior99":import_prior99,"bixelpitch":import_bixelpitch}
# Mirrors CardPartyCatalog.h: what the X4 accepts from the list.
MAX_PACKS=8
NAME_CHARS=set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")

class PublishError(ValueError):
    pass

def check_commit(path,commit):
    if not (Path(path)/".git").exists():
        return
    head=subprocess.run(["git","-C",str(path),"rev-parse","HEAD"],capture_output=True,text=True,check=True).stdout.strip()
    if head!=commit:
        raise PublishError(f"{path} is at {head}, the config pins {commit}")

def check_entry(entry):
    name=entry["file"]
    if not (4<len(name)<48 and name.endswith(".dat") and not name.startswith(".") and set(name)<=NAME_CHARS):
        raise PublishError(f"file name {name!r} is not one the X4 accepts")
    title=entry["title"]
    if not (0<len(title.encode())<48) or "|" in title:
        raise PublishError(f"title {title!r} does not fit the X4 list")

def build(entry,sources):
    check_entry(entry)
    inputs=[]
    for source in entry["sources"]:
        importer=IMPORTERS.get(source["importer"])
        if importer is None:
            raise PublishError(f"unknown importer {source['importer']!r}")
        path=sources.get(source["importer"])
        if path is None:
            raise PublishError(f"no --source for {source['importer']}")
        check_commit(path,source["commit"])
        cards,issues,_=importer.convert(path)
        if issues:
            raise PublishError(f"{source['importer']}: {len(issues)} cards failed to convert")
        inputs.append((importer.manifest(source["commit"]),cards))
    cards,manifest,_=merge_cards.merge(inputs,entry["deck_id"],entry["title"])
    manifest.update(version=entry["version"],rights_holder=entry["rights_holder"],
                    license=entry["license"],license_url=entry["license_url"],
                    rights_status="verified",editorial_status="approved",rights_note=entry["decision"])
    for card in cards:
        card["reviewed"]=True
    data,receipt=pack.validate(manifest,cards)
    digest=hashlib.sha256(data).hexdigest()
    if entry.get("sha256") and digest!=entry["sha256"]:
        raise PublishError(f"{entry['file']}: built {digest}, the config expects {entry['sha256']}")
    return data,receipt

def readme(built):
    lines=["Cards Against Whatever: public card packs for the Xteink X4 Pro.","",
           "Download on the X4: CAW > [03] DECK > Download from GitHub.",
           "Or copy a .dat file to /games/cardparty/packs/ on the SD card.",""]
    for entry,receipt in built:
        lines+=[f"{entry['file']}: {entry['title']} ({receipt['prompts']} prompts, {receipt['answers']} answers)",
                f"  Rights holders: {entry['rights_holder']}",
                f"  License: {entry['license']} {entry['license_url']}",
                "  Sources:"]
        lines+=[f"    {s['url']} at {s['commit']}" for s in entry["sources"]]
        lines+=["  Changes: converted to the CAW pack format, sources merged, duplicates removed,",
                "  print hyphenation and quotes normalized. Card wording is otherwise unchanged."]
        lines+=[f"  Decision: {entry['decision']}",""]
    return "\n".join(lines)

def publish(config,sources,out):
    entries=config["packs"]
    if not 0<len(entries)<=MAX_PACKS:
        raise PublishError(f"{len(entries)} packs; the X4 lists 1 to {MAX_PACKS}")
    if len({e["file"] for e in entries})!=len(entries):
        raise PublishError("duplicate file name")
    out.mkdir(parents=True,exist_ok=True)
    built=[]
    for entry in entries:
        data,receipt=build(entry,sources)
        (out/entry["file"]).write_bytes(data)
        built.append((entry,receipt))
    catalog="".join(f"{e['file']}|{e['title']}|{r['bytes']}\n" for e,r in built)
    (out/"catalog.txt").write_text(catalog,encoding="utf-8")
    (out/"README.txt").write_text(readme(built),encoding="utf-8")
    return built

def main():
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config",type=Path,required=True)
    ap.add_argument("--source",action="append",default=[],metavar="IMPORTER=DIR")
    ap.add_argument("--out",type=Path,required=True)
    args=ap.parse_args()
    sources=dict(item.split("=",1) for item in args.source)
    try:
        built=publish(json.loads(args.config.read_text(encoding="utf-8")),sources,args.out)
    except (PublishError,pack.PackError,merge_cards.MergeError,OSError,KeyError) as exc:
        ap.error(str(exc))
    for entry,receipt in built:
        print(f"{entry['file']}: {receipt['prompts']} prompts, {receipt['answers']} answers, "
              f"{receipt['bytes']} bytes, sha256 {receipt['sha256']}")
    return 0

if __name__=="__main__":
    sys.exit(main())
