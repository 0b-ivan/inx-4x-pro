#!/usr/bin/env bash
# End-to-end X4 Pro simulator test of the AWS study *activity*, not just UI
# renderers. Builds one ORIGINAL synthetic question (no Udemy material), loads
# it through the emulated SD card, answers it incorrectly and then correctly
# via Review Missed. The on-card byte must finish with SEEN and no MISSED flag.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
bin=".pio/build/simulator_x4_pro/program"
test -x "$bin" || { echo "FAIL: no SDL simulator binary $bin"; exit 1; }
command -v xvfb-run >/dev/null || { echo "FAIL: xvfb-run is missing"; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/sd/trivia/decks" ".pio/aws-study-ui-smoke"
log=".pio/aws-study-ui-smoke/run.log"
export AWS_SMOKE_PRIVATE_DIR="$tmp"
python3 - <<'PY'
import json, os
from pathlib import Path
import sys
sys.path.insert(0, str(Path.cwd() / "tools_local/trivia"))
import build_aws_study
root = Path(os.environ["AWS_SMOKE_PRIVATE_DIR"])
row = {
  "q": "Which AWS service is designed for durable object storage in a cloud architecture?",
  "options": ["Amazon S3", "Amazon EBS"],
  "correct": [0], "type": "single",
  "explanation": "Amazon S3 stores objects. Amazon EBS provides block storage.",
  "category": "Domain 1: SDLC Automation",
}
input_file = root / "synthetic.jsonl"
input_file.write_text(json.dumps(row) + "\n", encoding="utf-8")
count, size, _ = build_aws_study.build(input_file, root / "sd/trivia/decks/DOP-C02.dat")
# A second independent certificate is visible in the chooser. Its progress
# must not be modified when the learner studies the DOP-C02 questions.
other = dict(row)
other["q"] = "Which AWS service executes event-driven code without provisioning servers?"
other["correct"] = [1]
other["category"] = "Topic 1: Serverless patterns"
other_file = root / "synthetic-other.jsonl"
other_file.write_text(json.dumps(other) + "\n", encoding="utf-8")
build_aws_study.build(other_file, root / "sd/trivia/decks/SAA-C03.dat")
assert count == 1 and size > 50
print("Synthetic AWS SD pack installed; no paid questions involved.")
PY

# Screen coordinates are logical 480x800 GT911 touch pixels. The simulator
# starts into the REAL Trivia activity; each transition is followed by an
# independent screenshot. Pauses allow slower CI runners to paint the page.
script="8200:TAP:240,520,110;10000:TAP:240,145,110" # main -> packs -> DOP-C02
script+=";11800:TAP:240,240,110"  # begin study from DOP-C02 menu
script+=";13600:TAP:370,750,110"  # question -> first answer
script+=";15400:TAP:370,750,110"  # first -> second answer
script+=";17200:TAP:80,750,110"   # mark B (wrong)
script+=";19000:TAP:390,750,110"  # grade wrong
script+=";20800:TAP:75,750,110"   # end -> summary
script+=";22600:TAP:350,750,110"  # review missed
script+=";24400:TAP:370,750,110"  # question -> first answer
script+=";26200:TAP:80,750,110"   # mark A (right)
script+=";28000:TAP:370,750,110"  # next -> B
script+=";29800:TAP:390,750,110"  # grade correct
script+=";32000:TAP:350,750,110"  # finished -> completion summary
script+=";34600:QUIT"

shots="7500:.pio/aws-study-ui-smoke/main.bmp"
shots+=";9600:.pio/aws-study-ui-smoke/packs.bmp"
shots+=";11400:.pio/aws-study-ui-smoke/aws-menu.bmp"
shots+=";13100:.pio/aws-study-ui-smoke/question.bmp"
shots+=";17100:.pio/aws-study-ui-smoke/option-b.bmp"
shots+=";20400:.pio/aws-study-ui-smoke/wrong-review.bmp"
shots+=";22200:.pio/aws-study-ui-smoke/summary.bmp"
shots+=";24000:.pio/aws-study-ui-smoke/retry-question.bmp"
shots+=";31300:.pio/aws-study-ui-smoke/correct-review.bmp"
shots+=";33600:.pio/aws-study-ui-smoke/completed.bmp"

if ! env CROSSPLAY_AUTOSTART=TRIVIA CROSSPOINT_SIM_SD="$tmp/sd" \
  CROSSPOINT_SIM_INPUT_SCRIPT="$script" \
  CROSSPOINT_SIM_SCREENSHOTS="$shots" \
  LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 45s "$bin" >"$log" 2>&1; then
  echo "FAIL: AWS Trivia simulator flow"
  tail -100 "$log"
  exit 1
fi

python3 - "$tmp/sd/trivia/decks" .pio/aws-study-ui-smoke "$log" <<'PY'
from pathlib import Path
import hashlib, struct, sys
root, shots, log = map(Path, sys.argv[1:])
names = ["main", "packs", "aws-menu", "question", "option-b", "wrong-review",
         "summary", "retry-question", "correct-review", "completed"]
images = []
for name in names:
    path = shots / f"{name}.bmp"
    raw = path.read_bytes()
    assert raw[:2] == b"BM" and len(raw) > 1000, f"missing bitmap: {path}"
    dims = struct.unpack_from("<ii", raw, 18)
    assert tuple(map(abs, dims)) == (480, 800), f"wrong image size {dims}"
    images.append(hashlib.sha256(raw).hexdigest())
assert len(set(images)) >= 5, "AWS screen flow did not visit expected different views"
assert images[1] != images[2] and images[2] != images[3], "Deck selection or study start failed"
assert images[5] != images[8], "Incorrect and corrected review did not change"
assert images[8] != images[9], "Completed retry did not open summary screen"
state = (root / "DOP-C02.state").read_bytes()
assert state == b"\x01", f"Review Missed did not clear MISSED flag: {state.hex()}"
assert len((root / "DOP-C02.state.id").read_bytes()) == 4, "no pack identity stamp"
assert not (root / "SAA-C03.state").exists(), "Switching to DOP-C02 touched another certificate"
text = log.read_text(errors="replace")
assert "Autostart into TRIVIA" in text, "Trivia Activity not entered"
print("PASS X4 Pro AWS study SDL: menu -> wrong -> Review Missed -> correct, progress persisted")
PY
