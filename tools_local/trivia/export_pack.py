#!/usr/bin/env python3
"""Extract the public XTRIVIA pack.dat into a stable translation source JSONL.

The published binary has no question IDs. An ID is derived from the record
index plus the exact English clue, so every answer and all wrong options can
be validated against the source before the German pack is generated.
"""
import argparse
import hashlib
import json
from pathlib import Path

import pack_format


def export(pack_path, output_path):
    pack = pack_format.open_pack(str(pack_path))
    count = pack["count"]
    dest = Path(output_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    try:
        with tmp.open("w", encoding="utf-8") as output:
            for i in range(count):
                record = pack_format.read_one(pack, i)
                ident = hashlib.sha256(
                    str(i).encode("ascii") + b":" + record["q"].encode("utf-8")
                ).hexdigest()[:16]
                item = {"id": ident, **record}
                output.write(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n")
        tmp.replace(dest)
    finally:
        pack["f"].close()
        if tmp.exists():
            tmp.unlink()
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pack", required=True, help="English pack.dat from the existing GitHub release")
    ap.add_argument("--out", required=True, help="Translation source JSONL")
    args = ap.parse_args()
    n = export(args.pack, args.out)
    print(f"Extracted {n} questions from {args.pack} -> {args.out}")


if __name__ == "__main__":
    main()
