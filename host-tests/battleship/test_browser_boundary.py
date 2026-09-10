from pathlib import Path
root = Path(__file__).resolve().parents[2] / "src/apps_local/battleship/web"
server = (root / "BattleshipBrowserServer.cpp").read_text()
header = (root / "BattleshipBrowserServer.h").read_text()
page = (root / "BattleshipPage.html").read_text()
assert "bship::Game" not in server + header
assert "serializeSnapshot(snapshot_," in server
assert "type == WStype_TEXT" in server and "type == WStype_BIN" not in server
assert "ws_.close()" in server and "ws_.enableHeartbeat" in server
assert "parseCommand(payload, size, command)" in server
assert "strcmp(command.token, token_)" in server
assert "owner_ != client" in server
assert "broadcastTXT(reply_" not in server
assert "socket.onclose" in page and "socket.onmessage" in page
assert "selectedTarget===cell" in page and "send('fire',cell)" in page
assert "CONFIRM SURRENDER" in page and "send('surrender')" in page
assert "filter:brightness(0) invert(1)" in page
assert "id=\"placement\" hidden" in page
assert "mini-grid" in page
print("Browser boundary: DTO-only transport, validated commands, two-tap fire, surrender guard, staged UI and cleanup passed")
