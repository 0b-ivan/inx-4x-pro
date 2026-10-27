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
  "q": "S3. " + sentence * 7 + "Welche Loesung ist am einfachsten? FRAGE-ENDE.",
  "options": ["Amazon S3 mit Versionierung. A-ENDE.", "SQS Queue statt Objektablage. B-ENDE."],
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
left="SWIPE:360,370,120,370,250"
right="SWIPE:120,370,360,370,250"
t=8200; step=1800
s="$t:TAP:240,520,110"; shots=""
add() { t=$((t + step)); s+=";$t:$1"; }
shot() { shots+="${shots:+;}$((t + step - 300)):$out/$1.bmp"; }
add "TAP:240,145,110"                 # packs -> DOP-C02
add "TAP:240,240,110"; shot q1         # study -> question page 1
add "$up";             shot q2         # swipe up -> page 2
add "$down";           shot q1b        # swipe down -> page 1 again
add "TAP:24,165,900"; shot gloss       # hold directly on dotted "S3" in first line
add "TAP:370,750,110"; shot q1c        # RESUME -> same page 1
add "TAP:370,750,900"; shot q2b        # slow press MORE TEXT -> page 2
add "$up";             shot optA       # swipe up past question -> option A
add "TAP:80,750,110";  shot optAmark   # MARK A
add "TAP:80,750,110";  shot optAclear  # tap again -> UNMARK (single choice)
add "$left";            shot optB       # swipe left -> option B
add "$right";           shot optAagain  # swipe right -> option A
add "$left";            shot optBagain  # swipe left -> B again
add "TAP:80,750,110";  shot optBmark   # MARK B (wrong on purpose)
add "TAP:24,165,900"; shot glossB      # hold directly on dotted SQS in answer B
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
                       "optAclear", "optB", "optAagain", "optBagain", "optBmark", "glossB", "optBback",
                       "optBswipe", "review1", "review2", "review2b"]}
assert H["q1"] != H["q2"], "swipe up did not turn the question page"
assert H["q1b"] == H["q1"], "swipe down did not return to the identical first page"
assert H["gloss"] not in (H["q1"], H["q2"]), "hold in the text did not open the glossary"
assert H["q1c"] == H["q1"], "glossary RESUME did not restore the identical page"
assert H["q2b"] == H["q2"], "a slow press on MORE TEXT was swallowed"
assert H["optA"] not in (H["q1"], H["q2"]), "swipe past the question did not open option A"
assert H["optAmark"] != H["optA"], "MARK did not select option A"
assert H["optAclear"] == H["optA"], "single choice: tapping the marked option did not clear it"
assert H["optAagain"] == H["optA"], "swipe right did not restore option A"
assert H["optBagain"] == H["optB"], "swipe left did not restore option B"
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

# Fail early with checkpoint diagnostics instead of attributing a save failure
# to the later screenshot comparison.
python3 - "$tmp/sd/trivia/aws" <<'PY'
from pathlib import Path
import struct,sys
root=Path(sys.argv[1])
slots=sorted(root.glob("DOP-C02.session?"))
for slot in slots:
    data=slot.read_bytes()
    print(f"CHECKPOINT {slot.name} bytes={len(data)} hex={data.hex()}")
    if len(data)==48:
        print("  view,page,option,selected,generation,asked,right=",
              data[9],data[12],data[10],data[11],
              struct.unpack_from("<I",data,32)[0],
              struct.unpack_from("<I",data,36)[0],
              struct.unpack_from("<I",data,40)[0])
assert slots, "No checkpoints written before simulated deep-sleep restart"
PY

# Simulate real deep-sleep wake: kill the process, create a new Trivia Activity,
# keep the SD card, and verify that the SAME scored explanation page returns.
# No tap should be needed: the shelf already reopens Trivia on power-button wake.
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="14900:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="11600:$out/resumed-explanation.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 30s "$bin" >"$out/resume.log" 2>&1; then
  echo "FAIL: AWS session deep sleep simulated restore"
  tail -60 "$out/resume.log"
  exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, out = map(Path, sys.argv[1:])
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
if digest(out / "resumed-explanation.bmp") != digest(out / "review2b.bmp"):
    print("FAIL: screenshot differs after restart; inspect resume.log and checkpoint slots",
          file=sys.stderr)
    print((out / "resume.log").read_text(errors="replace")[-7000:], file=sys.stderr)
    for slot in sorted(root.glob("DOP-C02.session?")):
        raw = slot.read_bytes()
        print(f"{slot.name}: {len(raw)} bytes, hex={raw.hex()}", file=sys.stderr)
    raise AssertionError("Deep sleep did not restore identical graded explanation page")
assert (root / "DOP-C02.state").read_bytes() == b"\x05", (
    "Reopening an already graded question incorrectly graded it a second time")
slots = list(root.glob("DOP-C02.session?"))
assert slots, "no session checkpoint was persisted before standby"
print("PASS AWS study deep-sleep resume: scored explanation, reading page and progress")
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
# Persist non-default domains BEFORE the study run, so the checkpoint and
# .prefs agree when the simulated deep-sleep boot restores the session.
(root / "sd/trivia/aws/DOP-C02.prefs").write_bytes(b"\x07")
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

# Regression: reset is per-deck, requires a second confirmation, and must not
# erase the study deck, topic filters or the state of another certification.
# The multi-answer scenario above left DOP-C02.state == SEEN.
python3 - "$tmp/sd/trivia/aws" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
assert (root / "DOP-C02.state").read_bytes() == b"\x01"
assert (root / "DOP-C02.prefs").read_bytes() == b"\x07", "preconfigured domain mask changed"
(root / "SAA-C03.state").write_bytes(b"\x05")
# Leave the actual saved session in place. The simulator must resume it,
# explicitly finish the session, and THEN allow a confirmed progress reset.

PY
t=8200; s="$t:TAP:80,750,110"; shots="7600:$out/resetResumed.bmp"  # assert original explanation was restored
add "TAP:80,750,110"                  # summary MENU -> deck menu (checkpoint cleared)
add "TAP:240,620,110"; shot resetAsk  # selected deck -> reset confirmation
add "TAP:80,750,110";  shot resetNo   # cancel -> menu, nothing erased
add "TAP:240,620,110"                 # enter confirmation again
add "TAP:370,750,110"; shot resetYes  # explicit reset -> study menu
t=$((t + step)); s+=";$t:QUIT"
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="$s" CROSSPOINT_SIM_SCREENSHOTS="$shots" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 60s "$bin" >"$out/reset.log" 2>&1; then
  echo "FAIL: AWS reset progress UI simulator run"
  tail -60 "$out/reset.log"
  exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, out = map(Path, sys.argv[1:])
def digest(name):
    return hashlib.sha256((out / f"{name}.bmp").read_bytes()).digest()
assert digest("resetResumed") == digest("mreview"), "resume failed before reset flow; do not mistake a menu for a confirmation"
assert digest("resetAsk") != digest("resetNo"), "confirmation and study menu identical"
assert digest("resetAsk") != digest("resetYes"), "confirm screen remains after reset"
assert (root / "DOP-C02.state").read_bytes() == b"\x00", "selected deck progress was not reset"
assert (root / "DOP-C02.prefs").read_bytes() == b"\x07", "reset altered domain filters"
assert (root / "SAA-C03.state").read_bytes() == b"\x05", "reset modified another deck"
assert (root / "DOP-C02.dat").stat().st_size > 0, "reset deleted question pack"
assert not list(root.glob("DOP-C02.session?")), "reset left resumable checkpoint behind"

print("PASS X4 Pro AWS study UX SDL: confirmation, per-pack progress reset, preserved filters")
PY

# Further isolation: a different ORIGINAL multi-select pack, left *ungraded*
# when the simulated controller loses all RAM. Resuming must retain both marks.
rm -rf "$tmp/sd"
mkdir -p "$tmp/sd/trivia/aws"
python3 - <<'PY'
import json,os,sys
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/"tools_local/trivia"))
import build_aws_study
root=Path(os.environ["AWS_UX_PRIVATE_DIR"])
row={"q":"Which TWO AWS services provide persistent data storage?",
     "options":["Amazon S3","Amazon DynamoDB","AWS Lambda"],
     "correct":[0,1],"type":"multi",
     "explanation":"S3 is object storage; DynamoDB is a database.",
     "category":"Domain 2: Durable Resources"}
src=root/"standby-multi.jsonl"
src.write_text(json.dumps(row)+"\n",encoding="utf-8")
assert build_aws_study.build(src,root/"sd/trivia/aws/DOP-C02.dat")[0]==1
PY
t=8200; s="$t:TAP:240,520,110"; shots=""
add "TAP:240,145,110" # open pack
add "TAP:240,240,110" # begin
add "$up"             # option A
add "TAP:80,750,110"  # mark A
add "$left"; shot ungradedB  # option B, A remains marked
add "TAP:80,750,110"; shot ungradedBoth  # mark B, A+B both marked
t=$((t + step)); s+=";$t:QUIT"
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="$s" CROSSPOINT_SIM_SCREENSHOTS="$shots" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 35s "$bin" >"$out/ungraded.log" 2>&1; then
  echo "FAIL: ungraded multi-select setup"; tail -60 "$out/ungraded.log"; exit 1
fi
python3 - "$tmp/sd/trivia/aws" <<'PY'
from pathlib import Path
import sys,struct
root=Path(sys.argv[1])
assert (root/"DOP-C02.state").read_bytes()==b"\x01", "ungraded selections must not set MISSED"
slots=[p for p in root.glob("DOP-C02.session?") if p.stat().st_size==48]
assert slots,"ungraded session not persisted"
active=max(slots,key=lambda p:struct.unpack_from("<I",p.read_bytes(),32)[0])
data=active.read_bytes()
assert data[:8]==b"AWSSESS1" and data[9]==2 and data[11]==3, (
    f"saved view/marks not option B and A+B: {(data[9],data[11])}")
assert struct.unpack_from("<I",data,36)[0]==0, "marking answers accidentally scored the question"
PY

if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="14500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="11600:$out/ungradedResumed.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 25s "$bin" >"$out/ungraded-resume.log" 2>&1; then
  echo "FAIL: ungraded multi-select resume"; tail -60 "$out/ungraded-resume.log"; exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib,sys
root,out=map(Path,sys.argv[1:])
def digest(name): return hashlib.sha256((out/name).read_bytes()).digest()
if digest("ungradedResumed.bmp")!=digest("ungradedBoth.bmp"):
    print((out/"ungraded-resume.log").read_text(errors="replace")[-7000:],file=sys.stderr)
    raise AssertionError("Standby lost ungraded marks or current answer card")
assert (root/"DOP-C02.state").read_bytes()==b"\x01", "resume graded an ungraded question"
print("PASS AWS standby resume of ungraded multi-select: marked answers and option restored")
PY

# Fault injection against the REAL on-disk checkpoints. First corrupt just
# the newest checksum so the alternate valid generation MUST recover the exact
# screen and marks. Then truncate BOTH slots: the app must enter the menu, not
# apply torn data or assign a grade. Keep original slots for the bank-swap test.
python3 - "$tmp/sd/trivia/aws" "$tmp/session-backup" <<'PY'
from pathlib import Path
import struct, sys
root, backup = map(Path, sys.argv[1:])
backup.mkdir()
slots = sorted(root.glob("DOP-C02.session?"))
assert len(slots) == 2, "expected two alternate checkpoints before fault injection"
for p in slots:
    raw = p.read_bytes()
    assert len(raw) == 48 and raw[:8] == b"AWSSESS1", "invalid baseline checkpoint"
    (backup / p.name).write_bytes(raw)
latest = max(slots, key=lambda p: struct.unpack_from("<I", p.read_bytes(), 32)[0])
other = next(p for p in slots if p != latest)
old = other.read_bytes()
assert old[9] == 2 and old[10] == 1 and old[11] in (1, 3), (
    f"unexpected alternate slot before checksum injection: {old[9:12].hex()}")
(backup / "expected.txt").write_text(
    "ungradedBoth.bmp" if old[11] == 3 else "ungradedB.bmp", encoding="ascii")
broken = bytearray(latest.read_bytes())
broken[11] ^= 0x40  # Change marks without recomputing the FNV checksum.
latest.write_bytes(broken)
PY
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="14500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="11600:$out/corruptFallback.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 25s "$bin" >"$out/corrupt-fallback.log" 2>&1; then
  echo "FAIL: corrupt newest slot did not recover alternate"; tail -60 "$out/corrupt-fallback.log"; exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$tmp/session-backup" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, backup, out = map(Path, sys.argv[1:])
def digest(p): return hashlib.sha256(p.read_bytes()).digest()
expected = backup.joinpath("expected.txt").read_text(encoding="ascii")
assert digest(out / "corruptFallback.bmp") == digest(out / expected), (
    "corrupt newest slot failed to restore the exact prior valid answer view")
assert (root / "DOP-C02.state").read_bytes() == b"\x01", (
    "fallback changed persistent seen/missed progress")
assert "Resumed study question" in (out / "corrupt-fallback.log").read_text(errors="replace"), (
    "fallback screenshot matched but no checkpoint was restored")
print("PASS AWS checkpoint corruption: newest FNV mismatch falls back to alternate generation")
PY

python3 - "$tmp/sd/trivia/aws" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
slots = sorted(root.glob("DOP-C02.session?"))
assert len(slots) == 2
for slot in slots:
    slot.write_bytes(slot.read_bytes()[:13])  # Two torn writes.
PY
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="14500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="11600:$out/corruptBothHome.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 25s "$bin" >"$out/corrupt-both.log" 2>&1; then
  echo "FAIL: invalid slots did not safely return to menu"; tail -60 "$out/corrupt-both.log"; exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$tmp/session-backup" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, backup, out = map(Path, sys.argv[1:])
def digest(p): return hashlib.sha256(p.read_bytes()).digest()
assert digest(out / "corruptBothHome.bmp") not in (
    digest(out / "ungradedB.bmp"), digest(out / "ungradedBoth.bmp")), (
    "both invalid checkpoint slots still displayed an answer card")
log = (out / "corrupt-both.log").read_text(errors="replace")
assert "Autostart into TRIVIA" in log and "Resumed study question" not in log, (
    "invalid slots must not report restored session")
assert (root / "DOP-C02.state").read_bytes() == b"\x01", (
    "invalid checkpoint reset durable learning progress")
for original in backup.glob("DOP-C02.session?"):
    (root / original.name).write_bytes(original.read_bytes())
print("PASS AWS checkpoint corruption: two truncated slots safely return to menu")
PY

# Bank replacement is a deliberate hard boundary: never restore answers from a
# different question even when the filename and question count are unchanged.
python3 - <<'PY'
import json,os,sys
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/"tools_local/trivia"))
import build_aws_study
root=Path(os.environ["AWS_UX_PRIVATE_DIR"])
row={"q":"A DIFFERENT installed certification question after bank replacement?",
     "options":["EBS","S3","Lambda"],"correct":[0],"type":"single",
     "explanation":"This content is not the previous multi-select pack.",
     "category":"Domain 3: Replacement"}
src=root/"replaced-bank.jsonl"
src.write_text(json.dumps(row)+"\n",encoding="utf-8")
assert build_aws_study.build(src,root/"sd/trivia/aws/DOP-C02.dat")[0]==1
PY
if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="14500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="11500:$out/replacedBankHome.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 25s "$bin" >"$out/replaced-bank.log" 2>&1; then
  echo "FAIL: replaced bank safety smoke"; tail -60 "$out/replaced-bank.log"; exit 1
fi
python3 - "$tmp/sd/trivia/aws" "$out" <<'PY'
from pathlib import Path
import hashlib,sys
root,out=map(Path,sys.argv[1:])
def digest(name): return hashlib.sha256((out/name).read_bytes()).digest()
assert digest("replacedBankHome.bmp")!=digest("ungradedResumed.bmp"), (
  "replaced question bank reused previous answer card")
assert (root/"DOP-C02.state").read_bytes()==b"\x00", (
  "pack replacement should safely initialize a fresh seen-state")
print("PASS AWS standby resume safety: changed pack invalidates saved answer state")
PY
