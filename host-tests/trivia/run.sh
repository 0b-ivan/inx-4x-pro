#!/bin/sh
# Builds and runs the trivia tests. No device and no PlatformIO: TriviaCore is
# freestanding C++17, which is what lets the format, the chooser and the option
# shuffle all be checked without a panel.
#
# Two sub-suites, and the second is the one that matters most:
#   test_trivia    -- reader, chooser and shuffle, over a pack built in C++
#   test_realpack  -- the SAME reader over a pack written by the Python writer,
#                     which is the only check that the two halves of the format
#                     actually agree. A reader tested only against its own
#                     writer will agree with a format both of them get wrong.
#
#   host-tests/trivia/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-trivia-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/trivia
TOOLS=../../tools_local/trivia

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_trivia.cpp "$SRC/TriviaCore.cpp" -o "$BUILD_DIR/test_trivia"
"$BUILD_DIR/test_trivia"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_realpack.cpp "$SRC/TriviaCore.cpp" -o "$BUILD_DIR/test_realpack"

# A pack written by the real writer, with a manifest of what went in.
python3 - "$BUILD_DIR" "$TOOLS" <<'PY'
import sys, os, random
build, tools = sys.argv[1], sys.argv[2]
sys.path.insert(0, tools)
import pack_format
rng = random.Random(4)
items = []
for i in range(300):
    it = {'q': f'Clue {i} — about this thing with a café and a naïve rôle',
          'a': f'Answer {i}', 'd': 1 + i % 5, 'y': 1984 + i % 42}
    if i % 3 == 0:
        it['alt'] = [f'The Answer {i}']
    if i % 2 == 0:
        it['w'] = [f'Wrong {i}.{w}' for w in range(6)]
    items.append(it)
pack_format.write(items, os.path.join(build, 'real.dat'))
with open(os.path.join(build, 'real.tsv'), 'w', encoding='utf-8') as f:
    for it in items:
        f.write(f"{it['d']}\t{it['y']}\t{len(it.get('alt',[]))}\t{len(it.get('w',[]))}\t{it['q']}\t{it['a']}\n")
PY
"$BUILD_DIR/test_realpack" "$BUILD_DIR/real.dat" "$BUILD_DIR/real.tsv"

# Python reference writer, translation quality gates and binary round-trip.
python3 "$TOOLS/test_de_pack.py"
python3 test_de_ota_contract.py

# AWS binary format, exact multi-select grading, domain filters and pagination.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_aws_pack.cpp "$SRC/AwsStudyPack.cpp" "$SRC/TriviaCore.cpp" -o "$BUILD_DIR/test_aws_pack"
"$BUILD_DIR/test_aws_pack"
python3 "$TOOLS/test_aws_study.py"

# Publishing is gated against private-content leakage: generate only the two
# synthetic public demo packs, validate manifest and binary format.
python3 - "$TOOLS" <<'PY'
import pathlib, struct, subprocess, sys, tempfile
tools = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="x4pro-public-demo-") as t:
    out = pathlib.Path(t)
    subprocess.run([sys.executable, str(tools / "make_public_demo_packs.py"),
                    "--out", str(out)], check=True)
    names = (out / "catalog.txt").read_text().splitlines()
    assert names == ["AWS|DOP-C02-DEMO", "AWS|SAA-C03-DEMO"]
    for cert in ("DOP-C02-DEMO", "SAA-C03-DEMO"):
        raw = (out / (cert + ".dat")).read_bytes()
        assert raw[:8] == b"XAWSDEV\x00"
        assert struct.unpack_from("<I", raw, 12)[0] == 24
        assert len((out / (cert + ".topics")).read_text().splitlines()) == 6
        assert b"Udemy" not in raw and b"paid question" not in raw
print("PASS public synthetic GitHub demo pack assets and catalog")
PY


# Certificate-named independent study pack catalogue (no private content).
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_study_deck_catalog.cpp "$SRC/StudyDeckCatalog.cpp" \
  -o "$BUILD_DIR/test_study_deck_catalog"
"$BUILD_DIR/test_study_deck_catalog"

# Cross-language check: build a complete synthetic 150-question pack with the
# production Python builder and read it using the actual firmware C++ reader.
# This does not contain or fetch any purchased quiz content.
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_aws_realpack.cpp "$SRC/AwsStudyPack.cpp" "$SRC/TriviaCore.cpp" \
  -o "$BUILD_DIR/test_aws_realpack"
python3 - "$BUILD_DIR" "$TOOLS" <<'PY'
import json, pathlib, sys
build, tools = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
sys.path.insert(0, str(tools))
import build_aws_study
source = build / "aws-synthetic-study.jsonl"
with source.open("w", encoding="utf-8") as out:
    for i in range(150):
        body = (
            f"Scenario {i}: A company needs highly available AWS services. "
            "Choose the best configuration while considering deployments, "
            "resilience, observability, IAM, network isolation and automation. "
        ) * 3 + "Service details include café and availability zones."
        options = [
            "Amazon S3", "Amazon EBS", "Amazon EFS", "Amazon FSx",
            "AWS Lambda", "Amazon DynamoDB"
        ]
        correct = [0, 2] if i % 4 == 0 else [1]
        rec = {
            "q": body,
            "options": options,
            "correct": correct,
            "type": "multi" if len(correct) > 1 else "single",
            "explanation": "Explanation: the synthetic answer key is part of this format test only. "
                           "This is not a recommendation for a real AWS question.",
            "category": f"Domain {1 + (i % 6)}: Synthetic test domain",
        }
        out.write(json.dumps(rec, ensure_ascii=False) + "\n")
n, size, largest = build_aws_study.build(source, build / "aws-synthetic.dat")
assert n == 150 and size > 0 and largest <= build_aws_study.MAX_RECORD
PY
"$BUILD_DIR/test_aws_realpack" "$BUILD_DIR/aws-synthetic.dat"

