from pathlib import Path
root = Path(__file__).resolve().parents[2] / "src/apps_local/battleship/web"
server = (root / "BattleshipBrowserServer.cpp").read_text()
header = (root / "BattleshipBrowserServer.h").read_text()
page = (root / "BattleshipPage.html").read_text()
assert "bship::Game" not in server + header
assert "serializeSnapshot(snapshot_," in server
assert "WStype_TEXT" not in server and "WStype_BIN" not in server
assert "ws_.close()" in server and "ws_.enableHeartbeat" in server
assert "fetch(" not in page and ".send(" not in page
assert "socket.onclose" in page and "socket.onmessage" in page
print("Browser boundary: DTO-only transport, no game actions, reconnect and cleanup passed")
