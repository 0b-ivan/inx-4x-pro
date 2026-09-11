from pathlib import Path

p = Path('src/apps_local/battleship/web/BattleshipBrowserServer.cpp')
text = p.read_text()
old = '''      const size_t n = serializePeerName(opponentName_, reply_, sizeof(reply_));
      if (n != 0) ws_.broadcastTXT(reply_, n);'''
new = '''      char peerMessage[96]{};
      const size_t n = serializePeerName(opponentName_, peerMessage, sizeof(peerMessage));
      if (n != 0) ws_.broadcastTXT(peerMessage, n);'''
if old not in text:
    raise SystemExit('peer broadcast pattern not found')
p.write_text(text.replace(old, new, 1))
