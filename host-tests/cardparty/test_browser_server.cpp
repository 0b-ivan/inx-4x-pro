// X4 party browser adapter tested on native host with Battleship's fake Wi-Fi.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"
#include "../../src/apps_local/cardparty/web/CardPartyProtocol.h"

namespace devmode {
int depth = 0;
void pause() { ++depth; }
void resume() { --depth; }
}  // namespace devmode
using namespace cardparty;
using namespace cardparty::web;

struct Fixture {
  char values[130][32] = {};
  Answer white[130] = {};
  Prompt black[1] = {{"Why did production break? ____", 1, 0}};
  Deck deck{};
  Fixture() {
    for (int i = 0; i < 130; ++i) {
      std::snprintf(values[i], sizeof(values[i]), "Response %d", i);
      white[i].text = values[i];
    }
    deck = Deck{black, 1, white, 130};
  }
};

std::string lastWelcome(WebSocketsServer& ws, size_t from, int client) {
  const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
  for (size_t i = from; i < ws.messages.size(); ++i) {
    if (ws.recipients[i] != client) continue;
    const auto& s = ws.messages[i];
    if (s.rfind(prefix, 0) != 0) continue;
    return s.substr(prefix.size(), 32);
  }
  return {};
}
bool anyStateFor(WebSocketsServer& ws, int seat) {
  for (size_t i = 0; i < ws.messages.size(); ++i)
    if (ws.recipients[i] == seat && ws.messages[i].find("\"type\":\"state\"") != std::string::npos) return true;
  return false;
}
bool hasPrivateFor(WebSocketsServer& ws, int seat) {
  for (size_t i = 0; i < ws.messages.size(); ++i)
    if (ws.recipients[i] == seat && ws.messages[i].find("\"private\":") != std::string::npos) return true;
  return false;
}
void send(WebSocketsServer& ws, int seat, const std::string& data) {
  ws.event(WStype_TEXT, static_cast<uint8_t>(seat), data);
}
int main() {
  Fixture fixture;
  Game game;
  assert(game.configure(fixture.deck, Rules{5, 2, true}, 11));
  Sessions sessions(game);
  Server server(game, sessions, fixture.deck);
  history::Book fame;
  server.setHistory(&fame);
  server.setLanguage("de");
  devmode::pause();
  assert(server.begin(Server::Mode::Hotspot));
  assert(server.running() && server.hotspot());
  assert(std::strlen(server.password()) == 16);
  assert(WiFi.currentMode == WIFI_AP);
  assert(WiFi.maxSoftApClients >= 9 && WiFi.maxSoftApClients <= 10);
  assert(devmode::depth == 2);
  assert(std::string(server.url()).find("/cardparty") != std::string::npos);
  auto& ws = *WebSocketsServer::instance;
  // The ESP32 library must enforce Origin during WebSocket upgrade, not
  // merely after CONNECTED when a seat ticket might already be created.
  assert(ws.requiredHeaders.size() == 1);
  assert(ws.requiredHeaders[0] == "Origin");
  assert(ws.wouldAcceptOrigin("http://192.168.4.1"));
  assert(ws.wouldAcceptOrigin("http://192.168.4.1:80"));
  assert(!ws.wouldAcceptOrigin(nullptr));
  assert(!ws.wouldAcceptOrigin("https://192.168.4.1"));
  assert(!ws.wouldAcceptOrigin("http://attacker.example"));
  assert(!ws.wouldAcceptOrigin("null"));
  assert(!ws.wouldAcceptOrigin("http://192.168.4.1.evil.example"));
  auto& http = *WebServer::instance;
  assert(http.routes.find("/cardparty") != http.routes.end());
  http.routes.at("/cardparty")();

  std::string tokens[3];
  for (int i = 0; i < 3; ++i) {
    ws.event(WStype_CONNECTED, static_cast<uint8_t>(i));
    assert(ws.messages.back() == R"({"type":"hello","lang":"de"})");
    assert(ws.recipients.back() == i);
    const size_t mark = ws.messages.size();
    send(ws, i, std::string("{\"type\":\"join\",\"name\":\"") + char('A' + i) + "\"}");
    tokens[i] = lastWelcome(ws, mark, i);
    assert(tokens[i].size() == 32);
    assert(ws.messages[mark].find("\"lang\":\"de\"") != std::string::npos);
    assert(anyStateFor(ws, i) && hasPrivateFor(ws, i));
    // Invalid proof / roles may not obtain someone else's cards.
    assert(ws.messages.back().find("\"type\":\"state\"") != std::string::npos);
  }
  assert(game.publicView().phase == Phase::Lobby);
  assert(fame.players() == 3);
  send(ws, 0, "{\"type\":\"fame\",\"token\":\"" + tokens[0] + "\",\"page\":0}");
  assert(ws.messages.back().find("\"type\":\"fame\"") != std::string::npos);
  assert(ws.messages.back().find("\"players\":[") != std::string::npos);
  // Fame never hands out player IDs: sent back on join, one claims that identity.
  for (size_t i = 0; i < fame.players(); ++i) {
    char id[17] = {};
    encodePlayerId(fame.at(i).id, id);
    assert(ws.messages.back().find(id) == std::string::npos);
  }
  // An old tab tries to resume a ticket belonging to a previous match.
  // The browser must receive "expired" and return to JOIN, not retry forever.
  ws.event(WStype_CONNECTED, 9);
  const size_t staleAt = ws.messages.size();
  send(ws, 9, R"({"type":"resume","token":"ffffffffffffffffffffffffffffffff"})");
  assert(ws.messages.size() == staleAt + 1);
  assert(ws.recipients.back() == 9);
  assert(ws.messages.back() == R"({"type":"expired"})");
  ws.event(WStype_DISCONNECTED, 9);
  const size_t marker = ws.messages.size();
  send(ws, 3, R"({"type":"join","name":"Not connected"})");
  assert(ws.messages.size() == marker);  // unconnected socket is silently ignored
  // Binary frames are dropped rather than interpreted as JSON.
  ws.event(WStype_BIN, 0, R"({"type":"join","name":"attacker"})");
  assert(game.publicView().players[3].seated == false);

  assert(server.hostStart());
  const auto initial = game.publicView();
  assert(fame.players() == 3);
  for (uint8_t i = 0; i < 3; ++i) assert(fame.at(i).games == 1);
  // Discard and redraw: once per round, revision-checked, never the same card.
  {
    HandView before{}, after{};
    assert(game.privateHand(2, before));
    send(ws, 2, "{\"type\":\"swap\",\"token\":\"" + tokens[2] + "\",\"revision\":0,\"slot\":0}");
    assert(game.privateHand(2, after) && after.swapped && after.size == before.size);
    assert(after.cards[0] != before.cards[0]);
    for (uint8_t i = 1; i < after.size; ++i) assert(after.cards[i] == before.cards[i]);
    assert(ws.messages.back().find("\"swapped\":true") != std::string::npos);
    const size_t again = ws.messages.size();
    send(ws, 2, "{\"type\":\"swap\",\"token\":\"" + tokens[2] + "\",\"revision\":1,\"slot\":1}");
    assert(ws.messages[again].find("\"type\":\"error\"") != std::string::npos);
    HandView still{};
    assert(game.privateHand(2, still) && still.cards[1] == before.cards[1]);
    send(ws, 1, "{\"type\":\"strike\",\"token\":\"" + tokens[1] + "\",\"slot\":0}");
    assert(fame.pairs() == 0);  // "doesn't fit" voting is gone
  }
  // T8 regression: the ESP32 writable() low-water threshold is about 2873B.
  // Even twenty maximum-length, heavily JSON-escaped winning sentences must
  // be paginated without asking a socket to send a blocking oversized frame.
  const std::string longSentence(200, '\\');
  for (uint64_t id = 100; id < 120; ++id) {
    assert(fame.upsert(id, "StressTraveler", static_cast<uint32_t>(id)));
    assert(fame.roundWon(id, longSentence.c_str(), static_cast<uint32_t>(id)));
  }
  // 23 players -> 3 player pages; 20 sentences -> 10 quote pages.
  for (int page = 0; page < 13; ++page) {
    const size_t before = ws.messages.size();
    send(ws, 0, "{\"type\":\"fame\",\"token\":\"" + tokens[0] + "\",\"page\":" + std::to_string(page) + "}");
    assert(ws.messages.size() == before + 1);
    assert(ws.messages.back().find("\"type\":\"fame\"") != std::string::npos);
    assert(ws.messages.back().size() <= 2800);
    if (page >= 3) assert(ws.messages.back().find("\"section\":\"quotes\"") != std::string::npos);
  }
  assert(initial.czar == 0 && initial.pick == 1);
  const std::string wrong = "ffffffffffffffffffffffffffffffff";
  const size_t beforeRejected = ws.messages.size();
  send(ws, 1, "{\"type\":\"submit\",\"token\":\"" + wrong + "\",\"revision\":0,\"slots\":[0]}");
  assert(ws.messages.size() == beforeRejected + 1);
  assert(ws.messages.back().find("\"type\":\"error\"") != std::string::npos);
  assert(ws.messages.back().find("\"code\":\"rejected\"") != std::string::npos);
  assert(ws.messages.back().find("\"message\"") == std::string::npos);
  assert(game.publicView().submitted == 0);

  send(ws, 1, "{\"type\":\"submit\",\"token\":\"" + tokens[1] + "\",\"revision\":0,\"slots\":[0]}");
  assert(game.publicView().submitted == 1);
  assert(game.publicView().phase == Phase::Playing);
  const size_t beforeReplay = ws.messages.size();
  send(ws, 1, "{\"type\":\"submit\",\"token\":\"" + tokens[1] + "\",\"revision\":0,\"slots\":[0]}");
  // Rejected replay: error, then a resync of the authenticated seat only.
  assert(ws.messages.size() == beforeReplay + 2);
  assert(ws.messages[beforeReplay].find("\"type\":\"error\"") != std::string::npos);
  assert(ws.recipients.back() == 1);
  assert(ws.messages.back().find("\"type\":\"state\",\"revision\":1") != std::string::npos);
  assert(game.publicView().submitted == 1);

  send(ws, 2, "{\"type\":\"submit\",\"token\":\"" + tokens[2] + "\",\"revision\":1,\"slots\":[0]}");
  const auto judging = game.publicView();
  assert(judging.phase == Phase::Judging && judging.plays == 3);  // 2 players + Rando
  for (uint8_t i = 0; i < judging.plays; ++i) assert(judging.pile[i].author == kNone);
  for (size_t i = 0; i < ws.messages.size(); ++i) {
    const auto& msg = ws.messages[i];
    // No author field ever appears in anonymous judge snapshots.
    if (msg.find("\"phase\":\"Judging\"") != std::string::npos) assert(msg.find("\"author\":") == std::string::npos);
  }
  send(ws, 1, "{\"type\":\"judge\",\"token\":\"" + tokens[1] + "\",\"revision\":1,\"selected\":0}");
  assert(game.publicView().phase == Phase::Judging);
  send(ws, 0, "{\"type\":\"judge\",\"token\":\"" + tokens[0] + "\",\"revision\":0,\"selected\":0}");
  assert(game.publicView().phase == Phase::Reveal);
  assert(server.hostNext());
  assert(game.publicView().round == 2);

  // A page reload may open the new WebSocket before the old connection
  // closes. The valid proof must atomically move seat C onto socket 5.
  ws.event(WStype_CONNECTED, 5);
  const size_t handoverAt = ws.messages.size();
  send(ws, 5, "{\"type\":\"resume\",\"token\":\"" + tokens[2] + "\"}");
  const std::string moved = lastWelcome(ws, handoverAt, 5);
  assert(moved.size() == 32 && moved != tokens[2]);
  assert(game.publicView().players[2].connected);
  assert(!ws.disconnected.empty() && ws.disconnected.back() == 2);
  assert(ws.clients == 3);  // prior 3 players + new socket - closed old socket
  // A forced-close socket cannot receive responses or control the new seat,
  // even if its former client ID sends a queued frame.
  const size_t invalidAt = ws.messages.size();
  send(ws, 2, "{\"type\":\"submit\",\"token\":\"" + moved + "\",\"revision\":0,\"slots\":[0]}");
  assert(ws.messages.size() == invalidAt);
  // A late duplicate callback cannot disconnect the replacement.
  ws.callback(2, WStype_DISCONNECTED, nullptr, 0);
  assert(game.publicView().players[2].connected);
  assert(anyStateFor(ws, 5));

  // Disconnect preserves seat; resume rotates the ticket.
  ws.event(WStype_DISCONNECTED, 1);
  assert(!game.publicView().players[1].connected);
  ws.event(WStype_CONNECTED, 4);
  const size_t resumeAt = ws.messages.size();
  send(ws, 4, "{\"type\":\"resume\",\"token\":\"" + tokens[1] + "\"}");
  const std::string renewed = lastWelcome(ws, resumeAt, 4);
  assert(renewed.size() == 32 && renewed != tokens[1]);
  assert(game.publicView().players[1].connected);
  send(ws, 4, "{\"type\":\"submit\",\"token\":\"" + tokens[1] + "\",\"revision\":1,\"slots\":[0]}");
  assert(ws.messages.back().find("\"type\":\"error\"") != std::string::npos);
  const size_t disconnectedAt = ws.messages.size();
  send(ws, 1, "{\"type\":\"submit\",\"token\":\"" + renewed + "\",\"revision\":1,\"slots\":[0]}");
  assert(ws.messages.size() == disconnectedAt);  // disconnected sockets get no reply

  server.stop();
  assert(!server.running());
  assert(WiFi.currentMode == WIFI_OFF);
  assert(devmode::depth == 1);
  server.stop();
  devmode::resume();
  assert(devmode::depth == 0);

  // Actual 8-seat transport/score boundary: 7 human plays + Rando = 8.
  // The ninth socket may observe HTTP but must not claim a ninth game seat.
  {
    Game many;
    assert(many.configure(fixture.deck, Rules{5, 1, true}, 789));
    Sessions group(many);
    Server eight(many, group, fixture.deck);
    WiFi.mode(WIFI_STA);
    assert(eight.begin(Server::Mode::ExistingWifi));
    auto& bus = *WebSocketsServer::instance;
    assert(bus.wouldAcceptOrigin("http://192.168.4.1"));
    assert(!bus.wouldAcceptOrigin("http://external.example"));
    std::string keys[8];
    for (int i = 0; i < 8; ++i) {
      bus.event(WStype_CONNECTED, static_cast<uint8_t>(i));
      const size_t mark = bus.messages.size();
      send(bus, i, std::string("{\"type\":\"join\",\"name\":\"") + char('A' + i) + "\"}");
      keys[i] = lastWelcome(bus, mark, i);
      assert(keys[i].size() == 32);
      assert(hasPrivateFor(bus, i));
    }
    bus.event(WStype_CONNECTED, 8);
    const size_t before = bus.messages.size();
    send(bus, 8, R"({"type":"join","name":"Ninth"})");
    assert(bus.messages.size() == before + 1);
    assert(bus.messages.back().find("\"type\":\"error\"") != std::string::npos);
    assert(eight.hostStart());
    assert(many.publicView().expected == 7);
    for (int i = 1; i < 8; ++i) {
      send(bus, i, "{\"type\":\"submit\",\"token\":\"" + keys[i] + "\",\"revision\":0,\"slots\":[0]}");
    }
    const auto view = many.publicView();
    assert(view.phase == Phase::Judging);
    assert(view.plays == 8);
    for (uint8_t i = 0; i < view.plays; ++i) assert(view.pile[i].author == kNone);
    assert(bus.messages.back().find("\"private\":") != std::string::npos);
    send(bus, 0, "{\"type\":\"judge\",\"token\":\"" + keys[0] + "\",\"revision\":0,\"selected\":7}");
    assert(many.publicView().phase == Phase::Reveal);
    assert(eight.hostNext());
    // One round requested at a table of eight: everyone judges once.
    assert(many.rules().rounds == 8);
    assert(many.publicView().phase == Phase::Playing && many.publicView().round == 2);
    eight.stop();
    assert(WiFi.currentMode == WIFI_STA);
    assert(devmode::depth == 0);
  }
  std::puts("cardparty X4 server: HTTP, 3-8 seats, auth, anonymous scoring, reconnect and cleanup OK");
}
