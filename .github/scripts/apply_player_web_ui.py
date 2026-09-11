from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# Reject short/long IDs before indexing 32 hex characters.
replace_once(
    "src/apps_local/player/PlayerWebApi.cpp",
    '#include <cstdio>\n#include <ctime>',
    '#include <cstdio>\n#include <cstring>\n#include <ctime>',
)
replace_once(
    "src/apps_local/player/PlayerWebApi.cpp",
    '''bool parseId(const char* text, player::PlayerId& out) {
  if (text == nullptr) return false;''',
    '''bool parseId(const char* text, player::PlayerId& out) {
  if (text == nullptr || std::strlen(text) != player::PlayerId::kSize * 2U) return false;''',
)

# Escape the visible local player name before placing it in WebSocket JSON.
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '''void sendPlayerReply(WebServer& http, const playerweb::Reply& reply) {
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  http.send(reply.status, "application/json", reply.body.c_str());
}
''',
    '''void sendPlayerReply(WebServer& http, const playerweb::Reply& reply) {
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  http.send(reply.status, "application/json", reply.body.c_str());
}

size_t serializePeerName(const std::string& name, char* output, const size_t capacity) {
  if (output == nullptr || capacity < 28) return 0;
  size_t at = 0;
  const char prefix[] = "{\\\"type\\\":\\\"peer\\\",\\\"name\\\":\\\"";
  const char suffix[] = "\\\"}";
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i) output[at++] = prefix[i];
  for (const unsigned char ch : name) {
    if (ch < 0x20) continue;
    if (ch == '\"' || ch == '\\\\') {
      if (at + 2 + sizeof(suffix) > capacity) return 0;
      output[at++] = '\\\\';
      output[at++] = static_cast<char>(ch);
    } else {
      if (at + 1 + sizeof(suffix) > capacity) return 0;
      output[at++] = static_cast<char>(ch);
    }
  }
  if (at + sizeof(suffix) > capacity) return 0;
  for (size_t i = 0; i < sizeof(suffix) - 1; ++i) output[at++] = suffix[i];
  output[at] = '\\0';
  return at;
}
''',
)
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '''  const int n = snprintf(reply_, sizeof(reply_), "{\\\"type\\\":\\\"peer\\\",\\\"name\\\":\\\"%s\\\"}", opponentName_.c_str());
  if (n > 0 && static_cast<size_t>(n) < sizeof(reply_)) ws_.sendTXT(client, reply_, static_cast<size_t>(n));''',
    '''  const size_t n = serializePeerName(opponentName_, reply_, sizeof(reply_));
  if (n != 0) ws_.sendTXT(client, reply_, n);''',
)
replace_once(
    "src/apps_local/battleship/web/BattleshipBrowserServer.cpp",
    '''      const int n = std::snprintf(reply_, sizeof(reply_), "{\\\"type\\\":\\\"peer\\\",\\\"name\\\":\\\"%s\\\"}",
                                  opponentName_.c_str());
      if (n > 0 && static_cast<size_t>(n) < sizeof(reply_)) ws_.broadcastTXT(reply_, static_cast<size_t>(n));''',
    '''      const size_t n = serializePeerName(opponentName_, reply_, sizeof(reply_));
      if (n != 0) ws_.broadcastTXT(reply_, n);''',
)

# Always use the current Battleship session token. It rotates on disconnect.
replace_once(
    "src/apps_local/battleship/web/BattleshipPage.html",
    '''      async function pxToken() {
        if (pxTokenValue) return pxTokenValue;
        const response = await fetch('/battleship/session', {cache:'no-store'});
        pxTokenValue = (await response.text()).trim();
        return pxTokenValue;
      }''',
    '''      async function pxToken() {
        if (token) { pxTokenValue = token; return token; }
        const response = await fetch('/battleship/session', {cache:'no-store'});
        pxTokenValue = (await response.text()).trim();
        return pxTokenValue;
      }''',
)
