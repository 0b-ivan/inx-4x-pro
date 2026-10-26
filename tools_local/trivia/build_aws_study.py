#!/usr/bin/env python3
"""Build a private XAWSDEV v1 study pack from an English personal JSONL export.

No source questions, raw HAR files, images, or resulting .dat should ever be
committed to the public repository. The CrossPlay firmware reads the output
from /trivia/aws/pack.dat on its SD card. No network is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile

MAGIC = b"XAWSDEV\0"
HEADER = struct.Struct("<8sHBBI")
MAX_RECORD = 12 * 1024
DOMAIN = re.compile(r"^Domain ([1-6]):\s+\S")


def _require(cond, message):
    if not cond:
        raise ValueError(message)


def encode(row, line):
    if not isinstance(row, dict):
        raise ValueError(f"line {line}: question must be an object")
    m = DOMAIN.match(str(row.get("category", "")))
    _require(m is not None, f"line {line}: unknown exam domain")
    _require(row.get("type") in ("single", "multi"), f"line {line}: invalid question type")
    options = row.get("options")
    _require(isinstance(options, list) and 2 <= len(options) <= 6,
             f"line {line}: options must be a list of two to six strings")
    answers = row.get("correct")
    _require(isinstance(answers, list) and len(answers) > 0,
             f"line {line}: missing correct answer indexes")
    n = len(options)
    _require(all(type(a) is int and 0 <= a < n for a in answers) and
             len(set(answers)) == len(answers), f"line {line}: invalid answer indexes")
    _require(len(answers) < n, f"line {line}: all options marked correct")
    _require((row["type"] == "single") == (len(answers) == 1),
             f"line {line}: question type does not match answer count")

    mask = sum(1 << i for i in answers)
    fields = [row.get("q"), row.get("explanation")] + options
    _require(all(isinstance(s, str) and s.strip() and "\0" not in s
                 for s in fields), f"line {line}: invalid text field")
    _require(len({s.strip().casefold() for s in options}) == n,
             f"line {line}: duplicate options")
    rec = bytes([int(m.group(1)), n, mask, 0])
    for field in fields:
        rec += field.encode("utf-8") + b"\0"
    _require(len(rec) <= MAX_RECORD,
             f"line {line}: record is {len(rec)} bytes, limit {MAX_RECORD}")
    return rec


def build(src, out=None):
    source = Path(src).expanduser().resolve()
    root = Path(__file__).resolve().parents[2]
    _require(root not in source.parents and root != source,
             "personal questions must be stored outside this public repository")
    records = []
    seen = set()
    with source.open("r", encoding="utf-8-sig") as stream:
        for n, line in enumerate(stream, 1):
            if not line.strip():
                continue
            row = json.loads(line)
            rec = encode(row, n)
            key = hashlib.sha256(rec).digest()
            _require(key not in seen, f"line {n}: duplicate study record")
            seen.add(key)
            records.append(rec)
    _require(records, "input has no questions")
    indices = [0]
    for rec in records:
        indices.append(indices[-1] + len(rec))
    expected = HEADER.size + 4 * len(indices) + indices[-1]
    if out is not None:
        dst = Path(out).expanduser().resolve()
        _require(root not in dst.parents and dst != root,
                 "private AWS packs must be built outside the public repository")
        dst.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(prefix=".aws-pack-", dir=str(dst.parent))
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(HEADER.pack(MAGIC, 1, 0, 0, len(records)))
                stream.write(struct.pack(f"<{len(indices)}I", *indices))
                for rec in records:
                    stream.write(rec)
                stream.flush()
                os.fsync(stream.fileno())
            _require(Path(tmp).stat().st_size == expected, "binary size mismatch")
            os.replace(tmp, dst)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
    return len(records), expected, max(map(len, records))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="private imported JSONL outside repo")
    ap.add_argument("--out", help="private destination .dat file outside repo")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if not args.dry_run and not args.out:
        ap.error("--out is required unless --dry-run")
    try:
        n, size, max_record = build(args.src, None if args.dry_run else args.out)
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        ap.exit(1, f"Invalid AWS pack: {exc}\n")
    print(f"AWS pack validated: questions={n}, bytes={size}, largest_record={max_record}"
          + (" (dry-run)" if args.dry_run else ""))


if __name__ == "__main__":
    main()
