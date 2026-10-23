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
apps=("CLOCK" "DEVICE INFO" "OTP GENERATOR" "WIFI CONNECT" "DNS LOOKUP" "PING" "HOST SCANNER" "HTTP CLIENT" "MDNS BROWSER" "BLE PROXIMITY" "BLE SCANNER")
for app in "${apps[@]}"; do
  name="${app// /_}"
  log="$out/$name.log"
  frame="$out/$name.bmp"
  script="6000:QUIT"
  if [ "$app" = "OTP GENERATOR" ]; then
    script="1900:RIGHT;6000:QUIT"
  elif [ "$app" = "BLE SCANNER" ]; then
    # Short Confirm on first fixture device: open its synthetic GATT service list.
    script="1900:ENTER:100;6000:QUIT"
  elif [ "$app" = "HTTP CLIENT" ] || [ "$app" = "MDNS BROWSER" ]; then
    # Confirm toggles the HTTP method or starts mDNS discovery.
    script="1900:ENTER:650;6000:QUIT"
  elif [ "$app" = "CLOCK" ]; then
    script="1900:ENTER:650;6000:QUIT"
  elif [ "$app" = "DEVICE INFO" ] || [ "$app" = "WIFI CONNECT" ]; then
    script="1900:DOWN;6000:QUIT"
  fi
  shots="4300:$frame"
  if [ "$app" = "OTP GENERATOR" ] || [ "$app" = "HTTP CLIENT" ] || [ "$app" = "MDNS BROWSER" ] || [ "$app" = "BLE SCANNER" ]; then
    shots="1600:$out/${name}_before.bmp;4300:$frame"
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
# Regression: connected and disconnected Host Scanner must render different states.
# This only validates an actual visible state difference, not full scan semantics.
offline="$out/HOST_SCANNER_offline.bmp"
offline_log="$out/HOST_SCANNER_offline.log"
if ! env CROSSPLAY_AUTOSTART="HOST SCANNER" \
  CROSSPLAY_SIM_EPOCH=1704067200 \
  CROSSPLAY_SIM_WIFI_CONNECTED=0 \
  CROSSPOINT_SIM_INPUT_SCRIPT="6000:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="4300:$offline" \
  LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$offline_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: offline Host Scanner crashed"
  tail -100 "$offline_log"
  exit 1
fi
if ! grep -Fq "Autostart into HOST SCANNER" "$offline_log"; then
  echo "FAIL biscuit-ui-smoke: offline Host Scanner not started"
  exit 1
fi
python3 - "$out/HOST_SCANNER.bmp" "$offline" <<'PYOFF'
from pathlib import Path
import hashlib
import struct
import sys
online, offline = (Path(p) for p in sys.argv[1:])
assert online.is_file() and offline.is_file(), "Host Scanner state screenshots missing"
for shot in (online, offline):
    data = shot.read_bytes()
    assert data[:2] == b"BM" and len(data) > 1000, f"invalid BMP: {shot}"
    width, height = struct.unpack_from("<ii", data, 18)
    assert (abs(width), abs(height)) == (480, 800), f"unexpected dimensions: {shot}"
assert hashlib.sha256(online.read_bytes()).digest() != hashlib.sha256(offline.read_bytes()).digest(), \
    "Host Scanner online/offline screenshots identical"
print("PASS Host Scanner connected/disconnected UI differs")
PYOFF
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
python3 - <<'PYUI'
from pathlib import Path
import hashlib

root = Path(".pio/biscuit-ui-smoke")
for app in ("HTTP_CLIENT", "MDNS_BROWSER"):
    before, after = root / f"{app}_before.bmp", root / f"{app}.bmp"
    assert before.is_file() and after.is_file(), f"{app}: screenshots missing"
    assert hashlib.sha256(before.read_bytes()).digest() != hashlib.sha256(after.read_bytes()).digest(), (
        f"{app}: Confirm action did not change the rendered screen"
    )
    print(f"PASS {app}: Confirm changes rendered screen")
PYUI
# The BLE fixture with two advertisements must render differently from an empty scan.
ble_empty="$out/BLE_PROXIMITY_empty.bmp"
ble_empty_log="$out/BLE_PROXIMITY_empty.log"
if ! env CROSSPLAY_AUTOSTART="BLE PROXIMITY" \
  CROSSPLAY_SIM_BLE_SCENARIO=empty \
  CROSSPLAY_SIM_EPOCH=1704067200 \
  CROSSPOINT_SIM_INPUT_SCRIPT="6000:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="4300:$ble_empty" \
  LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$ble_empty_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: empty BLE Proximity simulator run"
  tail -100 "$ble_empty_log"
  exit 1
fi
grep -Fq "Autostart into BLE PROXIMITY" "$ble_empty_log" || {
  echo "FAIL biscuit-ui-smoke: BLE Proximity empty autostart not confirmed"
  exit 1
}
python3 - "$out/BLE_PROXIMITY.bmp" "$ble_empty" <<'PYBLE'
from pathlib import Path
import hashlib
import struct
import sys
normal, empty = [Path(p) for p in sys.argv[1:]]
for shot in (normal, empty):
    data = shot.read_bytes()
    assert data[:2] == b"BM" and len(data) > 1000, f"missing/invalid BMP {shot}"
    assert tuple(map(abs, struct.unpack_from("<ii", data, 18))) == (480, 800), f"bad dimensions {shot}"
assert hashlib.sha256(normal.read_bytes()).digest() != hashlib.sha256(empty.read_bytes()).digest(), \
    "BLE advertisements and empty scan rendered identical images"
print("PASS BLE fixture vs empty scan: UI state differs")
PYBLE
python3 - <<'PYSCANNER'
from pathlib import Path
import hashlib
root = Path(".pio/biscuit-ui-smoke")
before, after = root / "BLE_SCANNER_before.bmp", root / "BLE_SCANNER.bmp"
assert before.is_file() and after.is_file(), "BLE Scanner screenshots missing"
assert hashlib.sha256(before.read_bytes()).digest() != hashlib.sha256(after.read_bytes()).digest(), (
    "BLE Scanner Confirm did not change rendered screen"
)
print("PASS BLE Scanner: device selection changes GATT screen")
PYSCANNER
echo "PASS biscuit-ui-smoke: 11 activities, 17 screenshots, 4 interactions, BLE empty and Host Scanner offline states"
