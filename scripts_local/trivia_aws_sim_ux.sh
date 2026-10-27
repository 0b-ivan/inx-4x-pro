#!/usr/bin/env bash
# End-to-end X4 Pro simulator test of the AWS study reader's touch UX, driven
# through the REAL Trivia activity with ORIGINAL synthetic one-question packs
# (no purchased or licensed material). Checks, by screenshot identity and by
# the progress byte written to the emulated SD card:
#
#   * a long question is paged by swipes; a downward swipe returns to the
#     identical page, and a slow (long) press on a footer button still acts;
#   * a hold in the text opens the offline glossary; RESUME returns to the
#     identical page and the answer selection survives;
#   * single choice: tapping MARK on the marked option clears it again;
#   * swiping past the last option never grades, swiping past the end of the
#     explanation never deals the next question; only GRADE grades;
#   * multiple response: marks toggle independently, survive BACK, and
#     GRADE NOW on the question page grades the whole selection.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
bin=".pio/build/simulator_x4_pro/program"
test -x "$bin" || { echo "FAIL: no SDL simulator binary $bin"; exit 1; }
command -v xvfb-run >/dev/null || { echo "FAIL: xvfb-run is missing"; exit 1; }
out=".pio/aws-study-ux-smoke"
tmp="$(mktemp -d)"
trap 'code=$?; if [ "$code" -ne 0 ]; then
  echo "AWS UX smoke failed (exit $code). Simulator log:"
  tail -60 "$out/run.log" 2>/dev/null || true
fi; rm -rf "$tmp"' EXIT
rm -rf "$out"
mkdir -p "$tmp/sd/trivia/aws" "$out"
export AWS_UX_PRIVATE_DIR="$tmp"
python3 - <<'PY'
import json, os, sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd() / "tools_local/trivia"))
import build_aws_study
root = Path(os.environ["AWS_UX_PRIVATE_DIR"])
# Long enough for two 11px pages, with UTF-8 and glossary terms on page one.
sentence = ("Ein Team betreibt eine Anwendung mit Amazon EC2, speichert Berichte in "
            "Amazon S3 und vergibt Rechte ueber IAM-Rollen (Größe, café). ")
row = {
  "q": sentence * 7 + "Welche Loesung ist am einfachsten? FRAGE-ENDE.",
  "options": ["Amazon S3 mit Versionierung. A-ENDE.", "Amazon EBS Snapshots. B-ENDE."],
  "correct": [0], "type": "single",
  "explanation": ("Amazon S3 speichert Objekte dauerhaft; Amazon EBS ist Blockspeicher. " * 12)
                 + "ERKLAERUNG-ENDE.",
  "category": "Domain 1: SDLC Automation",
}
src = root / "ux.jsonl"
src.write_text(json.dumps(row, ensure_ascii=False) + "\n", encoding="utf-8")
count, _, _ = build_aws_study.build(src, root / "sd/trivia/aws/DOP-C02.dat")
assert count == 1
print("Synthetic AWS UX pack installed; no paid questions involved.")
PY

# Logical 480x800 touch pixels. TAP:x,y,ms -- 900 ms is a long press (the SDK
# threshold is 500 ms). SWIPE:x1,y1,x2,y2,ms.
up="SWIPE:240,520,240,240,250"
down="SWIPE:240,240,240,520,250"
t=8200; step=1800
s="$t:TAP:240,520,110"; shots=""
add() { t=$((t + step)); s+=";$t:$1"; }
shot() { shots+="${shots:+;}$((t + step - 300)):$out/$1.bmp"; }
add "TAP:240,145,110"                 # packs -> DOP-C02
add "TAP:240,240,110"; shot q1         # study -> question page 1
add "$up";             shot q2         # swipe up -> page 2
add "$down";           shot q1b        # swipe down -> page 1 again
add "TAP:240,300,900"; shot gloss      # hold in text -> glossary
add "TAP:370,750,110"; shot q1c        # RESUME -> same page 1
add "TAP:370,750,900"; shot q2b        # slow press MORE TEXT -> page 2
add "$up";             shot optA       # swipe up past question -> option A
add "TAP:80,750,110";  shot optAmark   # MARK A
add "TAP:80,750,110";  shot optAclear  # tap again -> UNMARK (single choice)
add "TAP:370,750,110"; shot optB       # NEXT -> option B
add "TAP:80,750,110";  shot optBmark   # MARK B (wrong on purpose)
add "TAP:240,300,900"; shot glossB     # hold -> glossary of option B
add "TAP:370,750,110"; shot optBback   # RESUME keeps the selection
add "$up";             shot optBswipe  # swipe past last option -> NO grading
add "TAP:390,750,110"; shot review1    # GRADE -> explanation page 1 (wrong)
add "$up";             shot review2    # page 2
add "$up";             shot review2b   # past the end -> NO next question
t=$((t + step)); s+=";$t:QUIT"

if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="$s" CROSSPOINT_SIM_SCREENSHOTS="$shots" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 90s "$bin" >"$out/run.log" 2>&1; then
  echo "FAIL: AWS study UX simulator run"
  exit 1
fi

python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib, struct, sys
root, shots = map(Path, sys.argv[1:])
def h(name):
    raw = (shots / f"{name}.bmp").read_bytes()
    assert raw[:2] == b"BM" and len(raw) > 1000, f"missing bitmap {name}"
    assert tuple(map(abs, struct.unpack_from("<ii", raw, 18))) == (480, 800), name
    return hashlib.sha256(raw).hexdigest()
H = {n: h(n) for n in ["q1", "q2", "q1b", "gloss", "q1c", "q2b", "optA", "optAmark",
                       "optAclear", "optB", "optBmark", "glossB", "optBback",
                       "optBswipe", "review1", "review2", "review2b"]}
assert H["q1"] != H["q2"], "swipe up did not turn the question page"
assert H["q1b"] == H["q1"], "swipe down did not return to the identical first page"
assert H["gloss"] not in (H["q1"], H["q2"]), "hold in the text did not open the glossary"
assert H["q1c"] == H["q1"], "glossary RESUME did not restore the identical page"
assert H["q2b"] == H["q2"], "a slow press on MORE TEXT was swallowed"
assert H["optA"] not in (H["q1"], H["q2"]), "swipe past the question did not open option A"
assert H["optAmark"] != H["optA"], "MARK did not select option A"
assert H["optAclear"] == H["optA"], "single choice: tapping the marked option did not clear it"
assert H["optBmark"] != H["optB"], "MARK did not select option B"
assert H["optBback"] == H["optBmark"], "glossary round trip lost the answer selection"
assert H["optBswipe"] == H["optBmark"], "a swipe past the last option changed the screen"
assert H["review1"] != H["optBmark"], "GRADE did not grade"
assert H["review2"] != H["review1"], "explanation did not page"
assert H["review2b"] == H["review2"], "a swipe past the explanation dealt a new question"
state = (root / "DOP-C02.state").read_bytes()
assert state == bytes([0x01 | 0x04]), f"expected SEEN|MISSED after one wrong GRADE, got {state.hex()}"
print("PASS X4 Pro AWS study UX SDL: paging, hold glossary, resume, unmark, no swipe grading")
PY

# Multiple response, on a fresh card: both keys must be marked, a mark can be
# taken back and set again, the selection survives BACK, and GRADE NOW on the
# question page grades the whole selection without visiting the last option.
rm -rf "$tmp/sd"
mkdir -p "$tmp/sd/trivia/aws"
python3 - <<'PY'
import json, os, sys
from pathlib import Path
sys.path.insert(0, str(Path.cwd() / "tools_local/trivia"))
import build_aws_study
root = Path(os.environ["AWS_UX_PRIVATE_DIR"])
row = {
  "q": "Which TWO services store data durably? (Choose two.)",
  "options": ["Amazon S3", "Amazon DynamoDB", "AWS Lambda"],
  "correct": [0, 1], "type": "multi",
  "explanation": "Amazon S3 and Amazon DynamoDB persist data; AWS Lambda runs code.",
  "category": "Domain 3: Resilient Cloud Solutions",
}
src = root / "multi.jsonl"
src.write_text(json.dumps(row) + "\n", encoding="utf-8")
assert build_aws_study.build(src, root / "sd/trivia/aws/DOP-C02.dat")[0] == 1
PY
t=8200; s="$t:TAP:240,520,110"; shots=""
add "TAP:240,145,110"
add "TAP:240,240,110"; shot mq           # question (one page)
add "$up";             shot mA           # option A
add "TAP:80,750,110";  shot mAmark       # MARK A
add "TAP:370,750,110"; shot mB           # NEXT -> B
add "TAP:80,750,110";  shot mBmark       # MARK B (A stays marked)
add "TAP:220,750,110"; shot mAback       # BACK -> A still SELECTED
add "TAP:80,750,110";  shot mAunmark     # UNMARK A
add "TAP:80,750,110";  shot mAremark     # MARK A again
add "$down";           shot mqback       # swipe down -> question, GRADE NOW visible
add "TAP:370,660,110"; shot mreview      # GRADE NOW from the question page
t=$((t + step)); s+=";$t:QUIT"
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="$s" CROSSPOINT_SIM_SCREENSHOTS="$shots" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 60s "$bin" >"$out/multi.log" 2>&1; then
  echo "FAIL: AWS multi-response UX simulator run"
  tail -60 "$out/multi.log"
  exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, shots = map(Path, sys.argv[1:])
H = {n: hashlib.sha256((shots / f"{n}.bmp").read_bytes()).hexdigest()
     for n in ["mq", "mA", "mAmark", "mB", "mBmark", "mAback", "mAunmark", "mAremark",
               "mqback", "mreview"]}
assert H["mAmark"] != H["mA"], "MARK did not select A"
assert H["mBmark"] != H["mB"], "MARK did not select B"
assert H["mAback"] == H["mAmark"], "BACK lost A's mark (multi-response must keep both)"
assert H["mAunmark"] != H["mAmark"], "UNMARK did not clear A"
assert H["mAremark"] == H["mAmark"], "re-marking A did not restore the selection"
assert H["mqback"] != H["mq"], "question page should offer GRADE NOW once answers are marked"
assert H["mreview"] not in (H["mqback"], H["mq"]), "GRADE NOW did not grade"
state = (root / "DOP-C02.state").read_bytes()
assert state == b"\x01", f"A+B is the key: expected SEEN without MISSED, got {state.hex()}"
print("PASS X4 Pro AWS study UX SDL: multi-response mark/unmark/back and GRADE NOW from question")
PY
