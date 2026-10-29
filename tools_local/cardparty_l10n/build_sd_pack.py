#!/usr/bin/env python3
"""Strictly gated CAW SD .dat compiler. Offline; never publishes/downloading cards.

Rights verification is a human process. A GitHub license label alone is NOT
sufficient. Draft output is marked so production firmware rejects it.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
from urllib.parse import urlsplit
import zlib

MAGIC=b"XCAWPK2\0"
FORMAT=2
# magic, format, flags, prompts, answers, body bytes, body CRC, deck version,
# category count, 3 zero bytes: 32 bytes. Layout: CardPartySdPack.h.
HEADER=struct.Struct("<8sHHHHIIIB3x")
MAX_BYTES=192*1024
MAX_PROMPT_BYTES=384
MAX_ANSWER_BYTES=160
MAX_META_BYTES=200
MAX_CATEGORIES=16
MAX_CATEGORY_NAME_BYTES=24
CATEGORY_DEFAULT_ON=1
CATEGORY_ADULT=2
ID=re.compile(r"^[a-z0-9][a-z0-9-]{2,63}$")
LICENSES={"CC0-1.0","CC-BY-4.0","CC-BY-NC-SA-2.0","CC-BY-NC-SA-4.0"}

class PackError(ValueError):
    pass

def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))

def load_cards(path):
    out=[]
    for n,line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(),1):
        if line.strip():
            try: out.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise PackError(f"invalid JSONL line {n}") from exc
    return out

def metadata_text(value):
    if not isinstance(value,str) or not value.strip():
        return False
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return not any(ord(ch)<32 or ord(ch)==127 for ch in value)

def web_url(value):
    if not metadata_text(value) or any(ch.isspace() for ch in value):
        return False
    try:
        url=urlsplit(value)
        return (url.scheme in ("https","http") and bool(url.hostname) and
                url.username is None and url.password is None and
                (url.port is None or 1<=url.port<=65535))
    except ValueError:
        return False

def length_prefixed(value,limit,label,*,allow_empty):
    if value is None and allow_empty:
        value=""
    if not isinstance(value,str) or (value and not metadata_text(value)) or (not value and not allow_empty):
        raise PackError(f"invalid {label}")
    raw=value.encode("utf-8")
    if len(raw)>limit:
        raise PackError(f"{label} too long")
    return struct.pack("<H",len(raw))+raw+b"\0"

def parse_categories(manifest):
    """Returns ({id: bit}, encoded table, receipt list)."""
    entries=manifest.get("categories",[])
    if not isinstance(entries,list) or len(entries)>MAX_CATEGORIES:
        raise PackError(f"categories must be a list of at most {MAX_CATEGORIES}")
    bits,table,receipt={},b"",[]
    for i,entry in enumerate(entries):
        if not isinstance(entry,dict):
            raise PackError("non-object category")
        cid=entry.get("id")
        if not isinstance(cid,str) or not ID.fullmatch(cid) or cid in bits:
            raise PackError("invalid or duplicated category id")
        default_on,adult=entry.get("default_on",True),entry.get("adult",False)
        if type(default_on) is not bool or type(adult) is not bool:
            raise PackError("category default_on/adult must be booleans")
        if adult and default_on:
            raise PackError(f"adult category {cid} must be off by default")
        flags=(CATEGORY_DEFAULT_ON if default_on else 0)|(CATEGORY_ADULT if adult else 0)
        table+=bytes([flags])+length_prefixed(entry.get("name"),MAX_CATEGORY_NAME_BYTES,
                                              f"category {cid} name",allow_empty=False)
        bits[cid]=1<<i
        receipt.append({"id":cid,"name":entry.get("name"),"default_on":default_on,"adult":adult})
    return bits,table,receipt

def card_mask(card,bits):
    ids=card.get("categories",[])
    if not isinstance(ids,list):
        raise PackError("card categories must be a list")
    mask=0
    for cid in ids:
        if cid not in bits:
            raise PackError(f"card {card.get('id')} names unknown category {cid!r}")
        mask|=bits[cid]
    return mask

def validate(manifest,cards,*,draft=False):
    if not isinstance(manifest,dict):
        raise PackError("manifest must be an object")
    name=manifest.get("deck_id","")
    if not isinstance(name,str) or not ID.fullmatch(name):
        raise PackError("unsafe deck_id")
    if manifest.get("source_locale")!="de":
        raise PackError("German source_locale required")
    version=manifest.get("version")
    if type(version) is not int or not 1<=version<=0xffffffff:
        raise PackError("invalid deck version")
    licence=manifest.get("license")
    if not draft and (not isinstance(licence,str) or licence not in LICENSES):
        raise PackError("unverified content license")
    if not draft and (
        manifest.get("rights_status")!="verified" or
        manifest.get("editorial_status")!="approved" or
        not metadata_text(manifest.get("rights_holder")) or
        not web_url(manifest.get("source_url")) or
        not web_url(manifest.get("license_url"))
    ):
        raise PackError("human rights/editorial approval, attribution and license URL are mandatory")
    # Shown on the X4 for attribution; a released pack must carry all of it.
    meta=b"".join(length_prefixed(manifest.get(field),MAX_META_BYTES,field,allow_empty=draft)
                  for field in ("title","license","license_url","rights_holder","source_url"))
    bits,category_table,category_receipt=parse_categories(manifest)
    if not isinstance(cards,list) or len(cards)<33:
        raise PackError("at least 1 prompt and 32 answers required")
    ids=set()
    prompts=[]
    answers=[]
    for card in cards:
        if not isinstance(card,dict):
            raise PackError("non-object card")
        if not draft and card.get("reviewed") is not True:
            raise PackError("all cards require explicit human review")
        cid=card.get("id")
        if not isinstance(cid,str) or not ID.fullmatch(cid) or cid in ids:
            raise PackError("invalid or duplicated card id")
        ids.add(cid)
        text=card.get("text")
        if not isinstance(text,str) or not text.strip():
            raise PackError("missing text")
        if any(ord(ch)<32 or ord(ch)==127 for ch in text):
            raise PackError("control characters, NUL or multiline card")
        try:
            raw=text.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise PackError("invalid Unicode card text") from exc
        kind=card.get("kind")
        if kind=="prompt":
            pick,draw=card.get("pick",1),card.get("draw",0)
            if type(pick) is not int or pick not in (1,2,3) or (
                type(draw) is not int or draw not in (0,1,2)):
                raise PackError("bad pick/draw")
            if text.count("____") not in (0,pick):
                raise PackError("pick/placeholder mismatch")
            if len(raw)>MAX_PROMPT_BYTES:
                raise PackError("prompt too long")
            prompts.append(struct.pack("<HBBH",len(raw),pick,draw,card_mask(card,bits))+raw+b"\0")
        elif kind=="answer":
            if "____" in text or len(raw)>MAX_ANSWER_BYTES:
                raise PackError("invalid or overlong answer")
            answers.append(struct.pack("<HH",len(raw),card_mask(card,bits))+raw+b"\0")
        else:
            raise PackError("unknown card kind")
    if not 1<=len(prompts)<=4096 or not 32<=len(answers)<=4096:
        raise PackError("invalid prompt/answer counts")
    body=meta+category_table+b"".join(prompts+answers)
    packed=HEADER.pack(MAGIC,FORMAT,0 if draft else 1,len(prompts),len(answers),
                       len(body),zlib.crc32(body),version,len(bits))+body
    if len(packed)>MAX_BYTES:
        raise PackError("pack exceeds device RAM/size budget")
    receipt={
        "id":name,"version":version,"format":FORMAT,"title":manifest.get("title"),
        "categories":category_receipt,"locale":"de","prompts":len(prompts),
        "answers":len(answers),"draft_only":draft,"bytes":len(packed),
        "sha256":hashlib.sha256(packed).hexdigest(),
        "source_url":manifest.get("source_url"),
        "rights_holder":manifest.get("rights_holder"),
        "license":licence,"license_url":manifest.get("license_url"),
        "rights_status":manifest.get("rights_status","unverified"),
        "editorial_status":manifest.get("editorial_status","unapproved"),
    }
    return packed,receipt

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest",type=Path,required=True)
    ap.add_argument("--cards",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    ap.add_argument("--draft",action="store_true",help="produces a test-only, firmware-rejected pack")
    args=ap.parse_args()
    try:
        blob,receipt=validate(load_json(args.manifest),load_cards(args.cards),draft=args.draft)
    except (PackError,OSError,UnicodeError,json.JSONDecodeError) as exc:
        ap.error(str(exc))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=args.out.parent,prefix=".caw-",delete=False) as f:
        temp=Path(f.name)
        try:
            f.write(blob);f.flush();os.fsync(f.fileno())
        except BaseException:
            temp.unlink(missing_ok=True)
            raise
    temp.replace(args.out)
    args.out.with_suffix(".receipt.json").write_text(
        json.dumps(receipt,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(receipt,ensure_ascii=False,indent=2))

if __name__=="__main__":
    main()
