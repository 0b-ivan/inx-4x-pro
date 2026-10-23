#!/usr/bin/env bash
# Start genuine Biscuit Activities in the native X4 Pro SDL simulator.
# Never open real WiFi/Internet connections: fake APs and DNS/TCP fixtures only.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
bin=".pio/build/simulator_x4_pro/program"
out=".pio/biscuit-ui-smoke"
test -x "$bin" || { echo "FAIL biscuit-ui-smoke: missing simulator binary $bin"; exit 1; }
command -v xvfb-run >/dev/null || { echo "FAIL biscuit-ui-smoke: xvfb-run missing"; exit 1; }
mkdir -p "$out"
apps=("CLOCK" "DEVICE INFO" "OTP GENERATOR" "WIFI CONNECT" "DNS LOOKUP" "PING")
for app in "${apps[@]}"; do
  name="${app// /_}"
  log="$out/$name.log"
  frame="$out/$name.bmp"
  script="6000:QUIT"
  if [ "$app" = "OTP GENERATOR" ]; then
    script="1900:RIGHT;6000:QUIT"
  elif [ "$app" = "CLOCK" ]; then
    script="1900:ENTER:650;6000:QUIT"
  elif [ "$app" = "DEVICE INFO" ] || [ "$app" = "WIFI CONNECT" ]; then
    script="1900:DOWN;6000:QUIT"
  fi
  shots="4300:$frame"
  if [ "$app" = "OTP GENERATOR" ]; then
    shots="1600:$out/OTP_GENERATOR_before.bmp;4300:$frame"
  fi
  echo "[biscuit-ui-smoke] $app"
  if ! env CROSSPLAY_AUTOSTART="$app" \
    CROSSPLAY_SIM_EPOCH=1704067200 \
    CROSSPLAY_SIM_WIFI_CONNECTED=1 \
    CROSSPOINT_SIM_INPUT_SCRIPT="$script" \
    CROSSPOINT_SIM_SCREENSHOTS="$shots" \
    LIBGL_ALWAYS_SOFTWARE=1 \
    xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL biscuit-ui-smoke: simulator exited nonzero for $app"
    tail -100 "$log"
    exit 1
  fi
  if ! grep -Fq "Autostart into $app" "$log"; then
    echo "FAIL biscuit-ui-smoke: autostart not confirmed for $app"
    tail -100 "$log"
    exit 1
  fi
  python3 - "$frame" <<'PY'
from pathlib import Path
import struct
import sys
p = Path(sys.argv[1])
assert p.is_file(), f"missing screenshot: {p}"
data = p.read_bytes()
assert data[:2] == b"BM" and len(data) > 1000, f"invalid screenshot: {p}"
w, h = struct.unpack_from("<ii", data, 18)
assert 320 <= abs(w) <= 1600 and 240 <= abs(h) <= 1600, f"unexpected dimensions {w}x{h}: {p}"
pixels = data[struct.unpack_from("<I", data, 10)[0]:]
assert len(set(pixels)) > 1, f"blank screenshot: {p}"
print(f"PASS screenshot {p.name} {w}x{h}")
PY
done
python3 - <<'PY'
from pathlib import Path
import hashlib
root = Path(".pio/biscuit-ui-smoke")
before = root / "OTP_GENERATOR_before.bmp"
after = root / "OTP_GENERATOR.bmp"
assert before.is_file() and after.is_file(), "OTP screenshots missing"
digest = lambda p: hashlib.sha256(p.read_bytes()).digest()
assert digest(before) != digest(after), "OTP RIGHT action did not change the rendered screen"
print("PASS OTP page interaction: screen changed after RIGHT")
PY
echo "PASS biscuit-ui-smoke: 6 activities, 7 screenshots, 1 interaction"
