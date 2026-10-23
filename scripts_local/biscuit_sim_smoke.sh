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
apps=("CLOCK" "DEVICE INFO" "OTP GENERATOR" "WIFI CONNECT" "DNS LOOKUP" "PING" "HOST SCANNER" "HTTP CLIENT" "MDNS BROWSER" "BLE PROXIMITY" "BLE SCANNER" "BLE CONTACT" "TRACKER DETECT" "PROBE SNIFFER" "CROWD DENSITY")
for app in "${apps[@]}"; do
  name="${app// /_}"
  log="$out/$name.log"
  frame="$out/$name.bmp"
  script="6000:QUIT"
  if [ "$app" = "OTP GENERATOR" ]; then
    script="1900:RIGHT;6000:QUIT"
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
  if [ "$app" = "CROWD DENSITY" ]; then
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
echo "PASS biscuit-ui-smoke: 15 activities, Crowd Density normal/empty/malformed, Probe Sniffer, tracker and BLE"

