#!/usr/bin/env python3
"""Watch an X4 Pro for a real reboot and capture MeshChat crash diagnostics.

Start this BEFORE entering MeshChat. Its ESP-NOW radio switch can make the
developer API temporarily unreachable without resetting the device. Detect
reboots using the device's uptime versus elapsed monotonic host time.

Uses wifi-flash.sh's private ~/.crossplay-devcode pairing-code cache. Tokens
and codes are never printed or put into the captured diagnostic files.
Only standard-library Python modules are required.
"""

import argparse
from datetime import datetime
import ipaddress
import json
import os
from pathlib import Path
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, Request, build_opener

HOME = Path.home()
OPENER = build_opener(ProxyHandler({}))  # Never send a LAN pairing code via a proxy.
TOKEN_FILE = HOME / ".crossplay-devtoken"
CODE_FILE = HOME / ".crossplay-devcode"


def request(ip, path, *, token="", form=None, timeout=3):
    body = urlencode(form).encode("ascii") if form is not None else None
    headers = {"Accept": "application/json"}
    if form is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    if token:
        headers["X-Dev-Token"] = token
    req = Request(f"http://{ip}{path}", data=body, headers=headers,
                  method="POST" if form is not None else "GET")
    try:
        with OPENER.open(req, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", errors="replace")
    except HTTPError as err:
        return err.code, err.read().decode("utf-8", errors="replace")
    except (URLError, TimeoutError, OSError):
        return None, ""


def save_private(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as dest:
            fd = -1
            dest.write(content)
    finally:
        if fd != -1:
            os.close(fd)


def pair(ip):
    # wifi-flash.sh cached this code when it flashed the image. Reuse it once
    # after each reboot; do not try random codes or repeatedly retry 401/429.
    try:
        code = CODE_FILE.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        raise RuntimeError("No ~/.crossplay-devcode found. Pair using wifi-flash.sh first.")
    if len(code) != 6 or not code.isascii() or not code.isdecimal():
        raise RuntimeError("Cached pairing code invalid; read the current code on the device.")
    status, content = request(ip, "/api/dev/pair", form={"code": code}, timeout=8)
    if status != 200:
        raise RuntimeError(f"Re-pair rejected (HTTP {status}); check current device code.")
    try:
        token = json.loads(content)["token"]
    except (ValueError, KeyError, TypeError) as err:
        raise RuntimeError("Re-pair response was not a token") from err
    if not isinstance(token, str) or len(token) != 32:
        raise RuntimeError("Re-pair returned an invalid token")
    save_private(TOKEN_FILE, token)
    print("Re-paired after reboot (token hidden)", flush=True)
    return token


def read_status(ip):
    status, content = request(ip, "/api/status", timeout=2)
    if status != 200:
        return None
    try:
        payload = json.loads(content)
        if payload.get("device") != "xteink_x4_pro":
            raise RuntimeError("Responding device is not an Xteink X4 Pro")
        payload["uptime"] = int(payload["uptime"])
        return payload
    except (ValueError, KeyError, TypeError):
        return None


def reset_detected(old_uptime, old_clock, uptime, clock):
    # The 3-second slack permits polling delay and uptime rounding without
    # confusing an ordinary Wi-Fi reconnect with a reboot.
    return uptime + 3 < old_uptime + (clock - old_clock)


def capture(ip, output_dir, event_number, status):
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    prefix = output_dir / f"x4pro-mesh-{stamp}-{event_number}"
    try:
        token = TOKEN_FILE.read_text(encoding="utf-8").strip()
    except FileNotFoundError:
        token = ""

    # The first 401 is expected after a reboot. Pair exactly once, then get
    # crash first (the device's RTC log ring retains only 16 lines).
    did_pair = False
    for attempt in range(20):
        http, body = request(ip, "/api/dev/crash", token=token)
        if http == 200:
            break
        if http == 401 and not did_pair:
            token = pair(ip)
            did_pair = True
            continue
        if http == 401:
            raise RuntimeError("New token rejected; not retrying pairing.")
        if http == 404:
            raise RuntimeError("Developer Mode is disabled; cannot fetch diagnostics.")
        time.sleep(1)
    else:
        raise RuntimeError("Crash API did not become ready after reboot.")

    try:
        report = json.loads(body)
    except ValueError as err:
        raise RuntimeError("Crash API returned invalid JSON") from err

    save_private(Path(str(prefix) + "-crash.json"),
                 json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    # Deliberately omit serial/IP from saved status: they are not needed here.
    status_fields = ("device", "version", "uptime", "freeHeap", "mode", "rssi")
    minimal = {name: status[name] for name in status_fields if name in status}
    save_private(Path(str(prefix) + "-status.json"),
                 json.dumps(minimal, ensure_ascii=False, indent=2) + "\n")

    http, logs = request(ip, "/api/dev/log", token=token)
    if http == 200:
        save_private(Path(str(prefix) + "-dev.log"), logs)
    else:
        print(f"Warning: dev/log returned HTTP {http}; crash report was saved.", flush=True)

    print("CAPTURED:", prefix, flush=True)
    print("  panicked:", report.get("panicked"),
          "resetReason:", report.get("resetReason"),
          "previousMeshCheckpoint:", report.get("previousMeshCheckpoint"),
          "logsValid:", report.get("logsValid"), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default="172.22.9.114", help="X4 Pro IPv4 address")
    parser.add_argument("--interval", type=float, default=1.0, help="Poll interval in seconds")
    parser.add_argument("--out-dir", type=Path, default=HOME / "Desktop")
    args = parser.parse_args()
    ipaddress.IPv4Address(args.ip)
    if args.interval < 0.5:
        parser.error("Minimum poll interval is 0.5 seconds.")
    if not args.out_dir.is_dir():
        parser.error("Output directory does not exist: " + str(args.out_dir))

    previous = None
    offline = False
    event_number = 0
    print(f"Watching X4 Pro at {args.ip}. Open MeshChat now; Ctrl+C to stop.", flush=True)
    try:
        while True:
            status = read_status(args.ip)
            now = time.monotonic()
            if status is None:
                if not offline:
                    print("OFFLINE (may be ESP-NOW; not proof of reboot)", flush=True)
                    offline = True
            else:
                uptime = status["uptime"]
                if previous is None:
                    print(f"BASELINE uptime={uptime}s", flush=True)
                elif reset_detected(previous[0], previous[1], uptime, now):
                    event_number += 1
                    print(f"REBOOT DETECTED: uptime {previous[0]}s -> {uptime}s",
                          flush=True)
                    try:
                        capture(args.ip, args.out_dir, event_number, status)
                    except RuntimeError as err:
                        print("Capture failed:", err, flush=True)
                elif offline:
                    print(f"ONLINE uptime={uptime}s (no reset proven)", flush=True)
                offline = False
                previous = (uptime, now)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nWatcher stopped.", flush=True)


if __name__ == "__main__":
    main()
