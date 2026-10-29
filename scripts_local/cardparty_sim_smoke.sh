#!/usr/bin/env bash
# Genuine SDL Activity with opt-in fake browser seats (no real TCP/Wi-Fi).
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
bin=".pio/build/simulator_x4_pro/program"
out=".pio/cardparty-simulator-smoke"
test -x "$bin"
command -v xvfb-run >/dev/null
mkdir -p "$out"
for scenario in setup lobby collect judge reveal finished; do
  screenshot="$out/$scenario.bmp"
  log="$out/$scenario.log"
  echo "[cardparty-sim] $scenario"
  env CROSSPLAY_AUTOSTART="CARDS WHATEVER" \
    CROSSPLAY_SIM_CARDPARTY_STATE="$scenario" \
    CROSSPLAY_SIM_EPOCH=1704067200 \
    CROSSPOINT_SIM_INPUT_SCRIPT="7200:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="5700:$screenshot" \
    LIBGL_ALWAYS_SOFTWARE=1 \
    xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 17s "$bin" >"$log" 2>&1 || { tail -90 "$log"; exit 1; }
  grep -Fq "Autostart into CARDS WHATEVER" "$log" || { tail -90 "$log"; exit 1; }
  if [ "$scenario" != setup ]; then
    grep -Fq "CARDPARTY_SIM_STATE=$scenario " "$log" || { tail -90 "$log"; exit 1; }
  fi
  test -s "$screenshot"
done
python3 - "$out" <<'PY'
from pathlib import Path
import hashlib,struct,sys
out=Path(sys.argv[1])
hashes={}
for case in ('setup','lobby','collect','judge','reveal','finished'):
    data=(out/(case+'.bmp')).read_bytes()
    assert data[:2]==b'BM' and len(data)>1000, case
    w,h=struct.unpack_from('<ii',data,18)
    assert (abs(w),abs(h)) == (480,800), (case,w,h)  # portrait, like the rest of the shelf
    pixels=data[struct.unpack_from('<I',data,10)[0]:]
    assert len(set(pixels))>1, (case,'blank bitmap')
    hashes[case]=hashlib.sha256(pixels).hexdigest()
    print('PASS CardParty SDL',case,hashes[case][:14])
assert len(set(hashes.values()))==len(hashes), 'UI phase screenshots indistinguishable'
print('PASS CardParty SDL six distinct application state screens')
PY
