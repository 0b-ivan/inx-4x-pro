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
apps=("CLOCK" "DEVICE INFO" "OTP GENERATOR" "WIFI CONNECT" "DNS LOOKUP" "PING" "HOST SCANNER" "HTTP CLIENT" "MDNS BROWSER" "BLE PROXIMITY" "BLE SCANNER" "BLE CONTACT" "TRACKER DETECT" "PROBE SNIFFER" "CROWD DENSITY" "DEV FINGERPRINT" "DEAUTH DETECTOR" "PACKET MONITOR" "NETWORK MONITOR" "BLE BEACON" "AIRTAG SPOOF" "WIFI DEAUTH" "BLE SPAM" "BLE KEYBOARD")
for app in "${apps[@]}"; do
  name="${app// /_}"
  log="$out/$name.log"
  frame="$out/$name.bmp"
  script="6000:QUIT"
  if [ "$app" = "OTP GENERATOR" ]; then
    script="1900:RIGHT;6000:QUIT"
  elif [ "$app" = "BLE KEYBOARD" ]; then
    script="1900:ENTER:100;2500:ENTER:100;3200:ENTER:100;6400:BACK:100;7300:BACK:100;8500:QUIT"
  elif [ "$app" = "BLE SPAM" ]; then
    script="1900:ENTER:100;2400:ENTER:100;4400:BACK:100;5400:BACK:100;6700:QUIT"
  elif [ "$app" = "WIFI DEAUTH" ]; then
    script="2200:ENTER:100;3800:BACK:100;4800:BACK:100;6200:QUIT"
  elif [ "$app" = "BLE BEACON" ]; then
    script="1900:ENTER:100;4700:BACK:100;5800:BACK:100;6900:QUIT"
  elif [ "$app" = "AIRTAG SPOOF" ]; then
    script="1900:DOWN:100;2400:ENTER:100;5500:BACK:100;6400:BACK:100;7500:QUIT"
  elif [ "$app" = "NETWORK MONITOR" ]; then
    script="2400:ENTER:100;3300:BACK:100;4200:BACK:100;5500:QUIT"
  elif [ "$app" = "PACKET MONITOR" ]; then
    script="1900:RIGHT:100;4700:BACK:100;6000:QUIT"
  elif [ "$app" = "DEAUTH DETECTOR" ]; then
    script="1900:ENTER:100;4500:BACK:100;6000:QUIT"
  elif [ "$app" = "DEV FINGERPRINT" ]; then
    script="1900:ENTER:100;4600:BACK:100;5800:QUIT"
  elif [ "$app" = "CROWD DENSITY" ]; then
    script="1900:ENTER:100;6600:BACK:100;7800:QUIT"
  elif [ "$app" = "PROBE SNIFFER" ]; then
    script="1900:ENTER:100;3200:BACK:100;4300:BACK:100;5800:QUIT"
  elif [ "$app" = "TRACKER DETECT" ]; then
    script="1900:ENTER:100;5300:BACK:100;6600:QUIT"
  elif [ "$app" = "BLE CONTACT" ]; then
    script="1900:DOWN:100;2400:ENTER:100;4400:BACK:100;5400:BACK:100;6800:QUIT"
  elif [ "$app" = "BLE SCANNER" ]; then
    # Scan -> Services -> Characteristics -> Value -> Back all the way to scan.
    script="1900:ENTER:100;2800:ENTER:100;3700:ENTER:100;4600:BACK:100;5500:BACK:100;6400:BACK:100;8000:QUIT"
  elif [ "$app" = "HTTP CLIENT" ] || [ "$app" = "MDNS BROWSER" ]; then
    # Confirm toggles the HTTP method or starts mDNS discovery.
    script="1900:ENTER:650;6000:QUIT"
  elif [ "$app" = "CLOCK" ]; then
    script="1900:ENTER:650;6000:QUIT"
  elif [ "$app" = "DEVICE INFO" ] || [ "$app" = "WIFI CONNECT" ]; then
    script="1900:DOWN;6000:QUIT"
  fi
  shots="4300:$frame"
  if [ "$app" = "BLE KEYBOARD" ]; then
    shots="1600:$out/${name}_scripts.bmp;2200:$out/${name}_preview.bmp;2950:$out/${name}_paired.bmp;4900:$out/${name}_executed.bmp;6800:$out/${name}_reset.bmp;8000:$frame"
  elif [ "$app" = "BLE SPAM" ]; then
    shots="1700:$out/${name}_disclaimer.bmp;2200:$out/${name}_menu.bmp;3700:$out/${name}_active.bmp;4900:$out/${name}_stopped.bmp;6000:$frame"
  elif [ "$app" = "WIFI DEAUTH" ]; then
    shots="1600:$out/${name}_targets.bmp;3200:$out/${name}_preview.bmp;4350:$out/${name}_stopped.bmp;5500:$frame"
  elif [ "$app" = "BLE BEACON" ]; then
    shots="1600:$out/${name}_menu.bmp;4200:$out/${name}_advertising.bmp;5400:$out/${name}_stopped.bmp;6300:$frame"
  elif [ "$app" = "AIRTAG SPOOF" ]; then
    shots="1700:$out/${name}_menu.bmp;3700:$out/${name}_active.bmp;5100:$out/${name}_rotated.bmp;6000:$frame"
  elif [ "$app" = "NETWORK MONITOR" ]; then
    shots="1800:$out/${name}_monitor.bmp;2800:$out/${name}_detail.bmp;3900:$out/${name}_back.bmp;5000:$frame"
  elif [ "$app" = "PACKET MONITOR" ]; then
    shots="1600:$out/${name}_initial.bmp;3500:$out/${name}_stats.bmp;5200:$frame"
  elif [ "$app" = "DEAUTH DETECTOR" ]; then
    shots="1600:$out/${name}_idle.bmp;3600:$out/${name}_monitor.bmp;5100:$frame"
  elif [ "$app" = "DEV FINGERPRINT" ]; then
    shots="1600:$out/${name}_ready.bmp;3600:$out/${name}_capturing.bmp;5000:$frame"
  elif [ "$app" = "CROWD DENSITY" ]; then
    shots="1600:$out/${name}_before.bmp;3100:$out/${name}_live.bmp;5150:$out/${name}_sampled.bmp;6000:$out/${name}_refilled.bmp;7100:$frame"
  elif [ "$app" = "PROBE SNIFFER" ]; then
    shots="1600:$out/${name}_list.bmp;2800:$out/${name}_detail.bmp;3800:$out/${name}_back.bmp;5200:$frame"
  elif [ "$app" = "TRACKER DETECT" ]; then
    shots="1600:$out/${name}_before.bmp;4200:$out/${name}_monitoring.bmp;6100:$frame"
  elif [ "$app" = "BLE CONTACT" ]; then
    shots="1600:$out/${name}_before.bmp;3200:$out/${name}_exchange.bmp;4000:$out/${name}_found.bmp;5000:$out/${name}_received.bmp;6100:$frame"
  elif [ "$app" = "BLE SCANNER" ]; then
    shots="1600:$out/${name}_before.bmp;2500:$out/${name}_services.bmp;3400:$out/${name}_characteristics.bmp;4300:$out/${name}_value.bmp;5200:$out/${name}_back_characteristics.bmp;6100:$out/${name}_back_services.bmp;7000:$frame"
  elif [ "$app" = "OTP GENERATOR" ] || [ "$app" = "HTTP CLIENT" ] || [ "$app" = "MDNS BROWSER" ]; then
    shots="1600:$out/${name}_before.bmp;4300:$frame"
  fi
  echo "[biscuit-ui-smoke] $app"
  if ! env CROSSPLAY_AUTOSTART="$app" \
    CROSSPLAY_SIM_BLE_TRACE=1 \
    CROSSPLAY_SIM_PROBE_TRACE=1 \
    CROSSPLAY_SIM_CROWD_TRACE=1 \
    CROSSPLAY_SIM_FINGERPRINT_TRACE=1 \
    CROSSPLAY_SIM_DEAUTH_TRACE=1 \
    CROSSPLAY_SIM_PACKET_TRACE=1 \
    CROSSPLAY_SIM_NETWORK_TRACE=1 \
    CROSSPLAY_SIM_KEYBOARD_TRACE=1 \
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
import struct

root = Path(".pio/biscuit-ui-smoke")
stages = [
    "before", "services", "characteristics", "value",
    "back_characteristics", "back_services", None,
]
frames = [root / (f"BLE_SCANNER_{stage}.bmp" if stage else "BLE_SCANNER.bmp")
          for stage in stages]
for frame in frames:
    assert frame.is_file(), f"BLE GATT stage screenshot missing: {frame}"
    content = frame.read_bytes()
    assert content[:2] == b"BM" and len(content) > 1000, f"invalid GATT frame: {frame}"
    assert tuple(map(abs, struct.unpack_from("<ii", content, 18))) == (480, 800), (
        f"unexpected GATT frame dimensions: {frame}"
    )
digests = [hashlib.sha256(frame.read_bytes()).digest() for frame in frames]
for stage, first, second in zip(stages[1:], digests, digests[1:]):
    assert first != second, f"BLE GATT view did not change at {stage!r}"

log = (root / "BLE_SCANNER.log").read_text(errors="replace")
states = [
    "services", "characteristics", "value",
    "back-characteristics", "back-services", "back-scanning",
]
pos = -1
for state in states:
    marker = "[BISCUIT_SIM] BLE_SCANNER:" + state
    pos = log.find(marker, pos + 1)
    assert pos >= 0, f"GATT navigation did not reach {state!r}; log: {log[-1000:]}"
print("PASS BLE Scanner: scan -> GATT services -> characteristics -> value -> back to scan")
PYSCANNER
python3 - <<'PYCONTACT'
from pathlib import Path
import hashlib, struct
root=Path(".pio/biscuit-ui-smoke")
steps=["before","exchange","found","received",None]
frames=[root/(f"BLE_CONTACT_{name}.bmp" if name else "BLE_CONTACT.bmp") for name in steps]
hashes=[]
for p in frames:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid contact screen: {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    hashes.append(hashlib.sha256(data).digest())
assert all(a!=b for a,b in zip(hashes,hashes[1:])), "Contact screen did not transition"
log=(root/"BLE_CONTACT.log").read_text(errors="replace")
pos=-1
for name in ("exchange:0","found:2","radio-off:2","received:2","idle:2"):
    pos=log.find("[BISCUIT_SIM] BLE_CONTACT:"+name,pos+1)
    assert pos>=0, f"BLE contact lifecycle missing {name}: {log[-800:]}"
print("PASS BLE Contact: two valid remote contacts and lifecycle")
PYCONTACT
empty_log="$out/BLE_CONTACT_empty.log"
if ! env CROSSPLAY_AUTOSTART="BLE CONTACT" \
  CROSSPLAY_SIM_BLE_TRACE=1 \
  CROSSPLAY_SIM_BLE_SCENARIO=empty \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:DOWN:100;2400:ENTER:100;4400:BACK:100;6000:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="5000:$out/BLE_CONTACT_empty.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$empty_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: BLE Contact empty scenario"
  tail -100 "$empty_log"
  exit 1
fi
for marker in "BLE_CONTACT:radio-off:0" "BLE_CONTACT:idle:0"; do
  grep -Fq "$marker" "$empty_log" || {
    echo "FAIL BLE Contact empty scan missing $marker"
    tail -100 "$empty_log"
    exit 1
  }
done

# Cancellation before the first poll must not leave the simulated BLE radio on.
cancel_log="$out/BLE_CONTACT_cancel.log"
if ! env CROSSPLAY_AUTOSTART="BLE CONTACT" \
  CROSSPLAY_SIM_BLE_TRACE=1 \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:DOWN:100;2400:ENTER:100;2850:BACK:100;4800:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="3800:$out/BLE_CONTACT_cancel.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$cancel_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: contact early cancel"
  tail -100 "$cancel_log"
  exit 1
fi
python3 - "$cancel_log" "$out/BLE_CONTACT_cancel.bmp" <<'PYCANCEL'
from pathlib import Path
import struct
import sys
log=Path(sys.argv[1]).read_text(errors="replace")
p=Path(sys.argv[2])
data=p.read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
states=["BLE_CONTACT:exchange:0","BLE_CONTACT:radio-off:0","BLE_CONTACT:idle:0"]
last=-1
for state in states:
    last=log.find(state,last+1)
    assert last>=0, f"early cancel missing {state}"
assert "BLE_CONTACT:found:2" not in log, "unexpected contact discovery before cancel"
print("PASS BLE Contact: early cancel, radio off, no contacts")
PYCANCEL

# An unavailable simulated BLE peripheral must never create a real host connection.
disconnect_log="$out/BLE_SCANNER_disconnect.log"
if ! env CROSSPLAY_AUTOSTART="BLE SCANNER" \
  CROSSPLAY_SIM_BLE_TRACE=1 \
  CROSSPLAY_SIM_BLE_SCENARIO=disconnect \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;4800:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="3800:$out/BLE_SCANNER_disconnect.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$disconnect_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: BLE Scanner disconnected scenario"
  tail -100 "$disconnect_log"
  exit 1
fi
grep -Fq "BLE_SCANNER:connect-failed" "$disconnect_log" || {
  echo "FAIL BLE Scanner: virtual connection error was not exercised"
  tail -100 "$disconnect_log"
  exit 1
}
python3 - "$out/BLE_SCANNER_disconnect.bmp" <<'PYFAIL'
from pathlib import Path
import struct
import sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
print("PASS BLE Scanner: synthetic connect failure")
PYFAIL
python3 - <<'PYTRACKER'
from pathlib import Path
import hashlib, struct
root=Path(".pio/biscuit-ui-smoke")
frames=[root/"TRACKER_DETECT_before.bmp",root/"TRACKER_DETECT_monitoring.bmp",root/"TRACKER_DETECT.bmp"]
digests=[]
for p in frames:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid tracker screenshot: {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    digests.append(hashlib.sha256(data).digest())
assert digests[0]!=digests[1] and digests[1]!=digests[2], "tracker must enter and exit monitoring"
log=(root/"TRACKER_DETECT.log").read_text(errors="replace")
for state in ("TRACKER:monitoring:0:0", "TRACKER:scan:3:", "TRACKER:radio-off:3:"):
    assert state in log, f"tracker normal path missing {state}: {log[-900:]}"
assert "TRACKER:alert:" not in log, "normal tracker scenario raised spurious alert"
print("PASS Tracker Detector: synthetic classification, monitoring and cleanup")
PYTRACKER
follower_log="$out/TRACKER_DETECT_follower.log"
if ! env CROSSPLAY_AUTOSTART="TRACKER DETECT" \
  CROSSPLAY_SIM_BLE_TRACE=1 \
  CROSSPLAY_SIM_TRACKER_SCENARIO=follower \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;5600:BACK:100;6900:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="1600:$out/TRACKER_DETECT_follower_before.bmp;5200:$out/TRACKER_DETECT_follower_alert.bmp;6400:$out/TRACKER_DETECT_follower_idle.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$follower_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: Tracker Detector follower scenario"
  tail -100 "$follower_log"; exit 1
fi
python3 - "$follower_log" "$out/TRACKER_DETECT_follower_alert.bmp" <<'PYFOLLOW'
from pathlib import Path
import struct,sys
log=Path(sys.argv[1]).read_text(errors="replace")
last=-1
for token in ("TRACKER:scan:1:3","TRACKER:alert:1:3","TRACKER:radio-off:1:3"):
    last=log.find(token,last+1)
    assert last>=0, f"missing follower state {token}: {log[-900:]}"
data=Path(sys.argv[2]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
print("PASS Tracker Detector: synthetic follower alert, clean stop")
PYFOLLOW
empty_log="$out/TRACKER_DETECT_empty.log"
if ! env CROSSPLAY_AUTOSTART="TRACKER DETECT" \
  CROSSPLAY_SIM_BLE_TRACE=1 \
  CROSSPLAY_SIM_TRACKER_SCENARIO=empty \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;4400:BACK:100;6000:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="5300:$out/TRACKER_DETECT_empty.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$empty_log" 2>&1; then
  echo "FAIL biscuit-ui-smoke: Tracker Detector empty scan"
  tail -100 "$empty_log"; exit 1
fi
grep -Fq "TRACKER:radio-off:0:" "$empty_log" || {
  echo "FAIL Tracker Detector empty scan leaked radio"
  tail -100 "$empty_log"; exit 1
}
if grep -Fq "TRACKER:alert:" "$empty_log"; then
  echo "FAIL Tracker Detector empty scan raised a false alarm"; exit 1
fi
python3 - <<'PYPROBE'
from pathlib import Path
import hashlib, struct
root=Path(".pio/biscuit-ui-smoke")
names=("list","detail","back",None)
frames=[root/(f"PROBE_SNIFFER_{name}.bmp" if name else "PROBE_SNIFFER.bmp") for name in names]
digests=[]
for p in frames:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid probe frame: {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    digests.append(hashlib.sha256(data).digest())
assert digests[0]!=digests[1] and digests[1]!=digests[2], "probe list/detail/back unchanged"
log=(root/"PROBE_SNIFFER.log").read_text(errors="replace")
pos=-1
for state in ("PROBE:scan:3","PROBE:detail:3","PROBE:back-list:3","PROBE:radio-off:3"):
    pos=log.find(state,pos+1)
    assert pos>=0, f"Probe Sniffer missing {state}: {log[-1000:]}"
print("PASS Probe Sniffer: list -> detail -> back -> radio off")
PYPROBE
for scenario in empty malformed; do
  log="$out/PROBE_SNIFFER_${scenario}.log"
  frame="$out/PROBE_SNIFFER_${scenario}.bmp"
  if ! env CROSSPLAY_AUTOSTART="PROBE SNIFFER" \
    CROSSPLAY_SIM_PROBE_TRACE=1 \
    CROSSPLAY_SIM_PROBE_SCENARIO="$scenario" \
    CROSSPOINT_SIM_INPUT_SCRIPT="2100:BACK:100;4200:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="3200:$frame" \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL Probe Sniffer $scenario"; tail -100 "$log"; exit 1
  fi
  grep -Fq "PROBE:scan:0" "$log" || {
    echo "FAIL Probe Sniffer $scenario did not scan"; exit 1
  }
  grep -Fq "PROBE:radio-off:0" "$log" || {
    echo "FAIL Probe Sniffer $scenario radio leak"; exit 1
  }
  if grep -Fq "PROBE:detail:" "$log"; then
    echo "FAIL Probe Sniffer $scenario false detail"; exit 1
  fi
  python3 - "$frame" <<'PYPROBEEMPTY'
from pathlib import Path
import struct, sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
PYPROBEEMPTY
done
python3 - <<'PYCROWD'
from pathlib import Path
import hashlib, struct
root=Path(".pio/biscuit-ui-smoke")
names=("before","live","sampled","refilled",None)
frames=[root/(f"CROWD_DENSITY_{n}.bmp" if n else "CROWD_DENSITY.bmp") for n in names]
digests=[]
for p in frames:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid Crowd Density BMP: {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    digests.append(hashlib.sha256(data).digest())
assert digests[0]!=digests[1] and digests[1]!=digests[2], "Crowd Density capture/window transition missing"
log=(root/"CROWD_DENSITY.log").read_text(errors="replace")
pos=-1
for state in ("CROWD:started:0:0", "CROWD:observed:3:0",
              "CROWD:sample:3:1", "CROWD:observed:3:1", "CROWD:radio-off:3:1"):
    pos=log.find(state,pos+1)
    assert pos>=0, f"Crowd Density missing/out-of-order {state}: {log[-1400:]}"
print("PASS Crowd Density: 3 unique MACs, window rollover, re-count and radio shutdown")
PYCROWD

# Empty and malformed input must not produce any counted MACs.
for scenario in empty malformed; do
  log="$out/CROWD_DENSITY_${scenario}.log"
  frame="$out/CROWD_DENSITY_${scenario}.bmp"
  if ! env CROSSPLAY_AUTOSTART="CROWD DENSITY" \
    CROSSPLAY_SIM_CROWD_TRACE=1 \
    CROSSPLAY_SIM_PROBE_SCENARIO="$scenario" \
    CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;3700:BACK:100;4900:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="3200:$frame" \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL Crowd Density $scenario test"; tail -100 "$log"; exit 1
  fi
  grep -Fq "CROWD:observed:0:0" "$log" || {
    echo "FAIL Crowd Density $scenario unexpectedly counted invalid frames"; exit 1
  }
  grep -Fq "CROWD:radio-off:0:0" "$log" || {
    echo "FAIL Crowd Density $scenario failed to release radio"; exit 1
  }
  python3 - "$frame" <<'PYCROWDNEG'
from pathlib import Path
import struct, sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
PYCROWDNEG
done
python3 - <<'PYFINGER'
from pathlib import Path
import hashlib,struct
out=Path(".pio/biscuit-ui-smoke")
screens=[out/"DEV_FINGERPRINT_ready.bmp",out/"DEV_FINGERPRINT_capturing.bmp",out/"DEV_FINGERPRINT.bmp"]
digests=[]
for frame in screens:
    data=frame.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"bad fingerprint BMP: {frame}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    digests.append(hashlib.sha256(data).digest())
assert digests[0]!=digests[1] and digests[1]!=digests[2], "fingerprint view did not change"
log=(out/"DEV_FINGERPRINT.log").read_text(errors="replace")
pos=-1
for state in ("ready:0:0","capturing:0:0","scan:5:9",
              "iOS:2:-50","Android:4:-62","Windows:1:-48",
              "Linux:1:-71","Unknown:1:-82","radio-off:5:0"):
    pos=log.find("[BISCUIT_SIM] FINGERPRINT:"+state,pos+1)
    assert pos>=0, f"fingerprint {state} missing or out of order: {log[-1400:]}"
print("PASS Device Fingerprinter: unique MACs, real OS heuristics, cleanup")
PYFINGER
for scenario in empty malformed; do
  log="$out/DEV_FINGERPRINT_$scenario.log"
  frame="$out/DEV_FINGERPRINT_$scenario.bmp"
  if ! env CROSSPLAY_AUTOSTART="DEV FINGERPRINT" \
    CROSSPLAY_SIM_FINGERPRINT_TRACE=1 \
    CROSSPLAY_SIM_FINGERPRINT_SCENARIO="$scenario" \
    CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;3500:BACK:100;4700:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="3000:$frame" \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL Device Fingerprinter $scenario"; tail -100 "$log"; exit 1
  fi
  for state in "FINGERPRINT:scan:0:0" "FINGERPRINT:radio-off:0:0"; do
    grep -Fq "$state" "$log" || {
      echo "FAIL fingerprint $scenario: $state absent"; tail -100 "$log"; exit 1
    }
  done
  python3 - "$frame" <<'PYBAD'
from pathlib import Path
import struct,sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
PYBAD
done
python3 - <<'PYDEAUTH'
from pathlib import Path
import hashlib, struct
root=Path(".pio/biscuit-ui-smoke")
pictures=[root/"DEAUTH_DETECTOR_idle.bmp",
          root/"DEAUTH_DETECTOR_monitor.bmp",root/"DEAUTH_DETECTOR.bmp"]
digests=[]
for p in pictures:
    b=p.read_bytes()
    assert b[:2]==b"BM" and len(b)>1000, f"bad Deauth Detector screenshot {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",b,18)))==(480,800)
    digests.append(hashlib.sha256(b).digest())
assert digests[0]!=digests[1] and digests[1]!=digests[2], (
    "monitoring/exit screens are not distinct")
log=(root/"DEAUTH_DETECTOR.log").read_text(errors="replace")
position=-1
for item in ("DEAUTH:ready:0:0","DEAUTH:monitoring:0:0",
             "DEAUTH:processed:2:1","DEAUTH:radio-off:2:1"):
    position=log.find("[BISCUIT_SIM] "+item,position+1)
    assert position>=0, f"missing or unordered Deauth Detector event: {item}"
assert "DEAUTH:alert:" not in log, "normal frames must not trigger alarm"
print("PASS Deauth Detector: normal management frames, no alert, cleanup")
PYDEAUTH

# A burst of synthetic management frames must activate the REAL alert path.
spike_log="$out/DEAUTH_DETECTOR_spike.log"
if ! env CROSSPLAY_AUTOSTART="DEAUTH DETECTOR" \
  CROSSPLAY_SIM_DEAUTH_TRACE=1 \
  CROSSPLAY_SIM_DEAUTH_SCENARIO=spike \
  CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;5200:BACK:100;6300:BACK:100;7500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="1600:$out/DEAUTH_DETECTOR_spike_idle.bmp;4700:$out/DEAUTH_DETECTOR_spike_alert.bmp;5700:$out/DEAUTH_DETECTOR_spike_dismissed.bmp;6900:$out/DEAUTH_DETECTOR_spike_exit.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$spike_log" 2>&1; then
  echo "FAIL Deauth Detector synthetic alert"; tail -100 "$spike_log"; exit 1
fi
python3 - "$spike_log" "$out/DEAUTH_DETECTOR_spike_alert.bmp" "$out/DEAUTH_DETECTOR_spike_dismissed.bmp" <<'PYDEAUTHSPIKE'
from pathlib import Path
import hashlib,struct,sys
log=Path(sys.argv[1]).read_text(errors="replace")
last=-1
for stage in ("DEAUTH:monitoring:0:0","DEAUTH:processed:6:1",
              "DEAUTH:alert:6:7","DEAUTH:dismissed:6:1",
              "DEAUTH:radio-off:6:1"):
    last=log.find(stage,last+1)
    assert last>=0, f"missing/out-of-order deauth spike {stage}: {log[-1200:]}"
frames=[]
for filename in sys.argv[2:]:
    data=Path(filename).read_bytes()
    assert data[:2]==b"BM" and len(data)>1000
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    frames.append(hashlib.sha256(data).digest())
assert frames[0]!=frames[1], "dismissal did not leave deauth spike display"
print("PASS Deauth Detector: 6-frame spike, alert, dismiss, radio off")
PYDEAUTHSPIKE

# No event should be recorded for empty/invalid fixtures, even via real onPacket().
for scenario in empty malformed; do
  log="$out/DEAUTH_DETECTOR_$scenario.log"
  image="$out/DEAUTH_DETECTOR_$scenario.bmp"
  if ! env CROSSPLAY_AUTOSTART="DEAUTH DETECTOR" \
    CROSSPLAY_SIM_DEAUTH_TRACE=1 \
    CROSSPLAY_SIM_DEAUTH_SCENARIO="$scenario" \
    CROSSPOINT_SIM_INPUT_SCRIPT="1900:ENTER:100;3900:BACK:100;5400:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="3200:$image" \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL Deauth Detector $scenario"; tail -100 "$log"; exit 1
  fi
  grep -Fq "DEAUTH:processed:0:0" "$log" || {
    echo "FAIL Deauth Detector $scenario counts invalid frames"; tail -100 "$log"; exit 1
  }
  grep -Fq "DEAUTH:radio-off:0:0" "$log" || {
    echo "FAIL Deauth Detector $scenario leaks radio"; tail -100 "$log"; exit 1
  }
  if grep -Fq "DEAUTH:alert:" "$log"; then
    echo "FAIL Deauth Detector $scenario phantom alert"; exit 1
  fi
  python3 - "$image" <<'PYDEAUTHNEG'
from pathlib import Path
import struct,sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
PYDEAUTHNEG
done
# Real Packet Monitor statistics with synthetic 802.11 management packets.
python3 - <<'PYPACKET'
from pathlib import Path
import hashlib,struct
root=Path(".pio/biscuit-ui-smoke")
files=[root/"PACKET_MONITOR_initial.bmp",root/"PACKET_MONITOR_stats.bmp",
       root/"PACKET_MONITOR.bmp"]
hashes=[]
for p in files:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid Packet Monitor screenshot {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    hashes.append(hashlib.sha256(data).digest())
assert hashes[0]!=hashes[1], "manual channel mode did not alter UI"
log=(root/"PACKET_MONITOR.log").read_text(errors="replace")
position=-1
for state in ("PACKET:monitoring:0:0","PACKET:processed:5:3",
              "PACKET:radio-off:5:3"):
    position=log.find(state,position+1)
    assert position>=0, f"Packet Monitor missing/ordering {state}: {log[-1200:]}"
print("PASS Packet Monitor: five virtual frames, 3 unique MACs, exit cleans radio")
PYPACKET

# Empty frames and a truncated frame must not fabricate a valid MAC.
for scenario in empty malformed; do
  log="$out/PACKET_MONITOR_$scenario.log"
  frame="$out/PACKET_MONITOR_$scenario.bmp"
  if ! env CROSSPLAY_AUTOSTART="PACKET MONITOR" \
    CROSSPLAY_SIM_PACKET_TRACE=1 \
    CROSSPLAY_SIM_PACKET_SCENARIO="$scenario" \
    CROSSPOINT_SIM_INPUT_SCRIPT="3400:BACK:100;4700:QUIT" \
    CROSSPOINT_SIM_SCREENSHOTS="2800:$frame" \
    LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
    timeout 18s "$bin" >"$log" 2>&1; then
    echo "FAIL Packet Monitor $scenario"; tail -100 "$log"; exit 1
  fi
  case "$scenario" in
    empty) expected="PACKET:processed:0:0"; stopped="PACKET:radio-off:0:0";;
    malformed) expected="PACKET:processed:1:0"; stopped="PACKET:radio-off:1:0";;
  esac
  grep -Fq "$expected" "$log" || {
    echo "FAIL Packet Monitor $scenario: expected $expected"; tail -100 "$log"; exit 1
  }
  grep -Fq "$stopped" "$log" || {
    echo "FAIL Packet Monitor $scenario: expected $stopped"; tail -100 "$log"; exit 1
  }
  python3 - "$frame" <<'PYPACKETNEG'
from pathlib import Path
import struct,sys
data=Path(sys.argv[1]).read_bytes()
assert data[:2]==b"BM" and len(data)>1000
assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
PYPACKETNEG
done
python3 - <<'PYNETMON'
from pathlib import Path
import struct,hashlib
root=Path(".pio/biscuit-ui-smoke")
names=("monitor","detail","back",None)
images=[root/(f"NETWORK_MONITOR_{name}.bmp" if name else "NETWORK_MONITOR.bmp") for name in names]
digest=[]
for file in images:
    bmp=file.read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000
    assert tuple(map(abs,struct.unpack_from("<ii",bmp,18)))==(480,800)
    digest.append(hashlib.sha256(bmp).digest())
assert digest[0]!=digest[1] and digest[1]!=digest[2], "frame/detail/back views unchanged"
log=(root/"NETWORK_MONITOR.log").read_text(errors="replace")
pos=-1
for stage in ("monitoring:0:0", "processed:4:3", "detail:4:3",
              "back-monitor:4:3", "radio-off:4:3"):
    pos=log.find("[BISCUIT_SIM] NETWORK:"+stage,pos+1)
    assert pos>=0, f"Network Monitor missing state {stage}: {log[-1100:]}"
print("PASS Network Monitor: frame aggregation, details, back navigation, cleanup")
PYNETMON

# Multi-phase virtual access point scan, including suspicious duplicate SSID.
rogue_log="$out/NETWORK_MONITOR_rogue.log"
if ! env CROSSPLAY_AUTOSTART="NETWORK MONITOR" \
  CROSSPLAY_SIM_NETWORK_TRACE=1 \
  CROSSPOINT_SIM_INPUT_SCRIPT="2100:ENTER:650;4900:ENTER:100;5500:BACK:100;6200:BACK:100;7500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="4300:$out/NETWORK_MONITOR_rogue_results.bmp;5200:$out/NETWORK_MONITOR_rogue_detail.bmp;5900:$out/NETWORK_MONITOR_rogue_back.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$rogue_log" 2>&1; then
  echo "FAIL Network Monitor rogue AP scenario"; tail -100 "$rogue_log"; exit 1
fi
python3 - "$rogue_log" "$out/NETWORK_MONITOR_rogue_results.bmp" "$out/NETWORK_MONITOR_rogue_detail.bmp" <<'PYROGUE'
from pathlib import Path
import struct,sys,hashlib
log=Path(sys.argv[1]).read_text(errors="replace")
assert "NETWORK:rogue-scan:0:1" in log, (
    "Long Confirm was treated as short (expected rogue AP scan). "
    + log[-1200:])
assert "NETWORK:rogue-results:3:1" in log, (
    "3-phase virtual rogue AP grouping did not complete. "
    + log[-1200:])
pictures=[]
for name in sys.argv[2:]:
    bmp=Path(name).read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000
    assert tuple(map(abs,struct.unpack_from("<ii",bmp,18)))==(480,800)
    pictures.append(hashlib.sha256(bmp).digest())
assert pictures[0]!=pictures[1], "rogue result and detail views identical"
assert "NETWORK:radio-off:" in log, "rogue AP test leaked radio"
print("PASS Network Monitor: 3 APs, suspicious SimLab group, result/detail/exit")
PYROGUE

empty_log="$out/NETWORK_MONITOR_empty.log"
if ! env CROSSPLAY_AUTOSTART="NETWORK MONITOR" \
  CROSSPLAY_SIM_NETWORK_TRACE=1 \
  CROSSPLAY_SIM_NETWORK_SCENARIO=empty \
  CROSSPOINT_SIM_INPUT_SCRIPT="3200:BACK:100;4500:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="2900:$out/NETWORK_MONITOR_empty.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$empty_log" 2>&1; then
  echo "FAIL Network Monitor empty scenario"; tail -100 "$empty_log"; exit 1
fi
grep -Fq "NETWORK:processed:0:0" "$empty_log" || {
  echo "FAIL Network Monitor counted events in empty scan"; exit 1
}
grep -Fq "NETWORK:radio-off:0:0" "$empty_log" || {
  echo "FAIL Network Monitor empty scenario leaked radio"; exit 1
}
# Simulated BLE advertising flows: no brand impersonation and no host radio.
python3 - <<'PYBLEADV'
from pathlib import Path
import hashlib,struct,re
root=Path(".pio/biscuit-ui-smoke")
checks={
  "BLE_BEACON":("BLE_BEACON_menu.bmp","BLE_BEACON_advertising.bmp",
                 "BLE_BEACON_stopped.bmp","BLE_BEACON.bmp"),
  "AIRTAG_SPOOF":("AIRTAG_SPOOF_menu.bmp","AIRTAG_SPOOF_active.bmp",
                  "AIRTAG_SPOOF_rotated.bmp","AIRTAG_SPOOF.bmp"),
}
for app,names in checks.items():
    digests=[]
    for name in names:
        data=(root/name).read_bytes()
        assert data[:2]==b"BM" and len(data)>1000, f"invalid {name}"
        assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
        digests.append(hashlib.sha256(data).digest())
    assert digests[0]!=digests[1] and digests[1]!=digests[2], (
        f"{app} did not change UI on start/stop")
    log=(root/(app+".log")).read_text(errors="replace")
    prefix="BLE_BEACON" if app=="BLE_BEACON" else "AIRTAG"
    assert re.search(rf"{prefix}:start:0:",log), f"missing start for {app}"
    assert re.search(rf"{prefix}:adv:[1-9][0-9]*:",log), f"missing synthetic preview for {app}"
    assert re.search(rf"{prefix}:radio-off:[0-9]+:",log), f"radio cleanup missing for {app}"
    if app=="AIRTAG_SPOOF":
        assert re.search(r"AIRTAG:rotated:[0-9]+:1",log), "virtual MAC rotation missing"
    else:
        assert re.search(r"BLE_BEACON:stop:[1-9][0-9]*:",log), "BLE beacon abort missing"
print("PASS BLE advertising preview modes, MAC rotation, cancel and radio cleanup")
PYBLEADV
python3 - <<'PYWIFIDRY'
from pathlib import Path
import hashlib,struct,re
folder=Path(".pio/biscuit-ui-smoke")
names=("targets","preview","stopped",None)
frames=[folder/(f"WIFI_DEAUTH_{n}.bmp" if n else "WIFI_DEAUTH.bmp") for n in names]
hashes=[]
for p in frames:
    bmp=p.read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000, f"bad simulated deauth BMP {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",bmp,18)))==(480,800)
    hashes.append(hashlib.sha256(bmp).digest())
assert hashes[0]!=hashes[1] and hashes[1]!=hashes[2], "deauth simulator UI states not distinct"
log=(folder/"WIFI_DEAUTH.log").read_text(errors="replace")
pos=-1
for stage in ("scan:0:2","started:0:2"):
    pos=log.find("WIFI_DEAUTH:"+stage,pos+1)
    assert pos>=0, f"WiFi Deauth missing {stage}"
assert re.search(r"WIFI_DEAUTH:preview:[1-9][0-9]*:1",log), "offline frames not counted"
assert re.search(r"WIFI_DEAUTH:stopped:[1-9][0-9]*:2",log), "dry-run cancel failed"
assert re.search(r"WIFI_DEAUTH:radio-off:[1-9][0-9]*:2",log), "radio not released"
print("PASS WiFi Deauth: synthetic target, frame counts, cancellation, no physical TX")
PYWIFIDRY

empty_log="$out/WIFI_DEAUTH_empty.log"
if ! env CROSSPLAY_AUTOSTART="WIFI DEAUTH" \
  CROSSPLAY_SIM_DEAUTH_TRACE=1 \
  CROSSPLAY_SIM_WIFI_DEAUTH_SCENARIO=empty \
  CROSSPOINT_SIM_INPUT_SCRIPT="2700:BACK:100;4200:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="2100:$out/WIFI_DEAUTH_empty.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$empty_log" 2>&1; then
  echo "FAIL offline WiFi Deauth empty target scenario"; tail -100 "$empty_log"; exit 1
fi
grep -Fq "WIFI_DEAUTH:scan:0:0" "$empty_log" || {
  echo "FAIL offline WiFi Deauth fabricated an AP"; exit 1
}
if grep -Fq "WIFI_DEAUTH:preview:" "$empty_log"; then
  echo "FAIL offline WiFi Deauth fabricated packets"; exit 1
fi
python3 - <<'PYSPAM'
from pathlib import Path
import hashlib,struct,re
root=Path(".pio/biscuit-ui-smoke")
names=("disclaimer","menu","active","stopped",None)
paths=[root/(f"BLE_SPAM_{n}.bmp" if n else "BLE_SPAM.bmp") for n in names]
hashes=[]
for p in paths:
    data=p.read_bytes()
    assert data[:2]==b"BM" and len(data)>1000, f"invalid BLE Spam preview screen {p}"
    assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800)
    hashes.append(hashlib.sha256(data).digest())
assert hashes[0]!=hashes[1] and hashes[1]!=hashes[2] and hashes[2]!=hashes[3], (
    "BLE Spam disclaimer/menu/preview/stop views not distinct")
log=(root/"BLE_SPAM.log").read_text(errors="replace")
assert "BLE_SPAM:start:0:4" in log, "default mixed preview mode not started"
for family in range(4):
    assert re.search(rf"BLE_SPAM:preview:[0-9]+:{family}",log), (
        f"missing synthetic advertising family {family}")
assert re.search(r"BLE_SPAM:stop:[1-9][0-9]*:4",log), "preview stop missing"
assert re.search(r"BLE_SPAM:radio-off:[1-9][0-9]*:4",log), "radio cleanup failed"
print("PASS BLE Spam: disclaimer, four in-memory families, stop and cleanup")
PYSPAM
python3 - <<'PYKEYBOARD'
from pathlib import Path
import re,struct,hashlib
root=Path(".pio/biscuit-ui-smoke")
names=("scripts","preview","paired","executed","reset",None)
screens=[root/(f"BLE_KEYBOARD_{n}.bmp" if n else "BLE_KEYBOARD.bmp") for n in names]
hashes=[]
for frame in screens:
    bmp=frame.read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000
    assert tuple(map(abs,struct.unpack_from("<ii",bmp,18)))==(480,800)
    hashes.append(hashlib.sha256(bmp).digest())
assert hashes[0]!=hashes[1] and hashes[1]!=hashes[2], (
    "BLE Keyboard script list/preview/virtual pairing unchanged")
log=(root/"BLE_KEYBOARD.log").read_text(errors="replace")
pos=-1
for marker in ("ready:0","paired:1","executing:14"):
    pos=log.find("BLE_KEYBOARD:"+marker,pos+1)
    assert pos>=0, f"virtual keyboard did not enter {marker}"
assert re.search(r"BLE_KEYBOARD:key:[1-9][0-9]*",log), "script generated no virtual HID report"
assert re.search(r"BLE_KEYBOARD:done:[1-9][0-9]*",log), "DuckyScript was not fully executed"
assert re.search(r"BLE_KEYBOARD:radio-off:[1-9][0-9]*",log), "virtual keyboard left radio enabled"
print("PASS BLE Keyboard: genuine built-in DuckyScript parse, virtual HID, no OS input")
PYKEYBOARD
# X4 Pro touch matrix: use the SDL simulator's actual capacitive touch
# injection (TAP), not virtual ENTER/BACK keys. Exercise real row hit-testing,
# bottom-row hint zones, list/detail/back and the Activity's cleanup lifecycle.
touch_log="$out/PROBE_SNIFFER_touch.log"
if ! env CROSSPLAY_AUTOSTART="PROBE SNIFFER" \
  CROSSPLAY_SIM_PROBE_TRACE=1 \
  CROSSPOINT_SIM_INPUT_SCRIPT="2100:TAP:100,150,120;2900:TAP:180,780,120;3850:TAP:60,780,120;4750:TAP:60,780,120;6100:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="1800:$out/PROBE_SNIFFER_touch_before.bmp;3350:$out/PROBE_SNIFFER_touch_detail.bmp;4300:$out/PROBE_SNIFFER_touch_back.bmp;5450:$out/PROBE_SNIFFER_touch_exit.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$touch_log" 2>&1; then
  echo "FAIL X4 Pro touch: Probe Sniffer TAP script"
  tail -100 "$touch_log"; exit 1
fi
python3 - "$touch_log" \
  "$out/PROBE_SNIFFER_touch_before.bmp" \
  "$out/PROBE_SNIFFER_touch_detail.bmp" \
  "$out/PROBE_SNIFFER_touch_back.bmp" \
  "$out/PROBE_SNIFFER_touch_exit.bmp" <<'PYTOUCH'
from pathlib import Path
import hashlib, re, struct, sys
log=Path(sys.argv[1]).read_text(errors="replace")
screens=[]
for name in sys.argv[2:]:
    bmp=Path(name).read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000, f"touch BMP missing: {name}"
    assert tuple(map(abs, struct.unpack_from("<ii",bmp,18)))==(480,800)
    screens.append(hashlib.sha256(bmp).hexdigest())
assert screens[0]!=screens[1], "touching hint Select did not enter detail"
assert screens[1]!=screens[2], "touching hint Back did not return to list"
assert screens[2]!=screens[3], "touching hint Exit did not leave Activity"
assert re.search(r"PROBE:touch-row:[0-9]+",log), "X4 Pro list-row tap ignored"
position=-1
for event in ("PROBE:touch-detail:3","PROBE:touch-back:3",
              "PROBE:touch-exit:3","PROBE:radio-off:3"):
    position=log.find(event,position+1)
    assert position>=0, f"X4 Pro touch flow missing {event}: {log[-1300:]}"
print("PASS X4 Pro touch: row tap, Select/Back/Exit hints and radio cleanup")
PYTOUCH
# A second X4 Pro touch path using real Network Monitor event navigation.
# This guards against apps continuing to use the touch-theme's zero-height
# ordinary hint bar instead of their own always-drawn footer.
network_touch_log="$out/NETWORK_MONITOR_touch.log"
if ! env CROSSPLAY_AUTOSTART="NETWORK MONITOR" \
  CROSSPLAY_SIM_NETWORK_TRACE=1 \
  CROSSPOINT_SIM_INPUT_SCRIPT="2200:TAP:180,780,120;3250:TAP:60,780,120;4350:TAP:60,780,120;6100:QUIT" \
  CROSSPOINT_SIM_SCREENSHOTS="1750:$out/NETWORK_MONITOR_touch_events.bmp;2800:$out/NETWORK_MONITOR_touch_detail.bmp;3800:$out/NETWORK_MONITOR_touch_return.bmp;5350:$out/NETWORK_MONITOR_touch_exit.bmp" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x1024x24' \
  timeout 18s "$bin" >"$network_touch_log" 2>&1; then
  echo "FAIL X4 Pro touch: Network Monitor TAP navigation"
  tail -100 "$network_touch_log"; exit 1
fi
python3 - "$network_touch_log" \
  "$out/NETWORK_MONITOR_touch_events.bmp" \
  "$out/NETWORK_MONITOR_touch_detail.bmp" \
  "$out/NETWORK_MONITOR_touch_return.bmp" \
  "$out/NETWORK_MONITOR_touch_exit.bmp" <<'PYNETTOUCH'
from pathlib import Path
import hashlib, struct, sys
log=Path(sys.argv[1]).read_text(errors="replace")
screens=[]
for name in sys.argv[2:]:
    bmp=Path(name).read_bytes()
    assert bmp[:2]==b"BM" and len(bmp)>1000, f"network touch frame missing: {name}"
    assert tuple(map(abs,struct.unpack_from("<ii",bmp,18)))==(480,800)
    screens.append(hashlib.sha256(bmp).digest())
assert screens[0]!=screens[1], "touch Select did not open network-event detail"
assert screens[1]!=screens[2], "touch Back did not return to network-event list"
assert screens[2]!=screens[3], "touch Exit did not leave Network Monitor"
pos=-1
for state in ("NETWORK:monitoring:0:0","NETWORK:processed:4:3",
              "NETWORK:radio-off:4:3"):
    pos=log.find(state,pos+1)
    assert pos>=0, f"Network Monitor touch missing {state}: {log[-1400:]}"
print("PASS X4 Pro touch: Network Monitor event/detail/back/exit, cleanup")
PYNETTOUCH
echo "PASS biscuit-ui-smoke: 24 activities, virtual BLE Keyboard script and prior regressions"


