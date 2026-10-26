from pathlib import Path
root = Path(__file__).resolve().parents[2]
web = root / "src/apps_local/battleship/web"
server = (web / "BattleshipBrowserServer.cpp").read_text()
header = (web / "BattleshipBrowserServer.h").read_text()
page = (web / "BattleshipPage.html").read_text()
snapshot = (web / "BrowserSnapshot.cpp").read_text()
activity = (web / "BattleshipLocalPlayActivity.cpp").read_text()
screens = (root / "src/apps_local/battleship/BattleshipScreens.cpp").read_text()
screens_header = (root / "src/apps_local/battleship/BattleshipScreens.h").read_text()
assert "bship::Game" not in server + header
assert "serializeSnapshot(snapshot_," in server
assert "serializeFleetStatus(snapshot_," in server
assert "hitsByX4Ship" in snapshot and "fleet.ships[ship]" in snapshot
assert "sendOpponentName(client)" in server and 'type\\\":\\\"peer' in server
assert "publishPlacement(const PlacementView& view)" in server
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
assert "grid-template-columns:repeat(3,minmax(0,1fr))" in page
assert "side-panel.active" in page
assert "impact-shake" in page and "ship-flash" in page and "ship-sunk-flash" in page
assert "sunk-segment" in page and "eventBanner" in page
assert "d.type==='f'" in page and "d.type==='peer'" in page
assert "ActionSurrender = 7" in screens_header
assert "ActionPlayAgain" in activity and "rematchFromX4()" in activity
assert "ActionSurrender" in activity and "surrenderX4()" in activity
assert "bship::defeated(browserGame_.side[0]) ? \"YOU LOSE\" : \"YOU WIN\"" in activity
assert 'secondary.label = "PLAY AGAIN"' in screens
assert 'secondary.label = model.surrenderArmed ? "CONFIRM" : "SURRENDER"' in screens
print("Browser boundary: DTO-only transport, peer identity, sunk feedback, X4 outcome, surrender/rematch, two-tap fire and cleanup passed")
