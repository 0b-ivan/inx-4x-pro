#!/usr/bin/env python3
"""Guard the real X4 Pro WebSocketsServer client pool.

The fake-native network tests do not model the arduinoWebSockets default
WEBSOCKETS_SERVER_CLIENT_MAX=5, so this checks the firmware build flags.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
config = (root / "platformio.ini").read_text(encoding="utf-8")
section = re.search(r"(?ms)^\[x4pro_common\]\s*\n(.*?)(?=^\[|\Z)", config)
assert section is not None, "missing x4pro_common build environment"
flags = re.findall(r"-DWEBSOCKETS_SERVER_CLIENT_MAX=(\d+)", section.group(1))
assert len(flags) == 1, f"expected one explicit pool size, found: {flags}"
limit = int(flags[0])
assert 9 <= limit <= 16, f"pool {limit} cannot host 8 players plus reconnect"
assert "$"+"{x4pro_common.build_flags}" in config, "x4pro env missing shared flags"
print(f"cardparty X4 Pro: {limit} WebSocket slots for 8 seats plus reconnect")
