// Host controls over the real wire: pause rights, frozen moves, kick, host
// end, "again" votes and the clock fields the phones count down from.
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"
#include "../../src/apps_local/cardparty/web/CardPartyProtocol.h"

namespace devmode {
int depth = 0;
void pause() { ++depth; }
void resume() { --depth; }
}  // namespace devmode
using namespace cardparty;
using namespace cardparty::web;

static int checks = 0, failures = 0;
#define CHECK(x)                                                \
  do {                                                          \
    ++checks;                                                   \
    if (!(x)) {                                                 \
      ++failures;                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    }                                                           \
  } while (0)

namespace {
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

struct Party {
  Fixture fixture;
  Game game;
  Sessions sessions{game};
  Server server{game, sessions, fixture.deck};
  WebSocketsServer* ws = nullptr;
  std::string tokens[4];
  uint32_t revisions[4] = {};

  Party() {
    clockMs = 1000;
    CHECK(game.configure(fixture.deck, Rules{5, 3, false}, 11));
    CHECK(server.begin(Server::Mode::Hotspot));
    ws = WebSocketsServer::instance;
    for (int i = 0; i < 4; ++i) {
      ws->event(WStype_CONNECTED, static_cast<uint8_t>(i));
      const size_t mark = ws->messages.size();
      send(i, std::string("{\"type\":\"join\",\"name\":\"P") + char('0' + i) + "\"}");
      tokens[i] = welcomeToken(mark, i);
      CHECK(tokens[i].size() == 32);
    }
    CHECK(server.hostStart());
  }
  ~Party() { server.stop(); }
  void send(int client, const std::string& data) { ws->event(WStype_TEXT, static_cast<uint8_t>(client), data); }
  std::string welcomeToken(size_t from, int client) const {
    const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
    for (size_t i = from; i < ws->messages.size(); ++i)
      if (ws->recipients[i] == client && ws->messages[i].rfind(prefix, 0) == 0)
        return ws->messages[i].substr(prefix.size(), 32);
    return {};
  }
  std::string last(int client) const {
    for (size_t i = ws->messages.size(); i-- > 0;)
      if (ws->recipients[i] == client) return ws->messages[i];
    return {};
  }
  bool lastHas(int client, const std::string& needle) const { return last(client).find(needle) != std::string::npos; }
  void ticketed(int client, const char* type) {
    send(client, std::string("{\"type\":\"") + type + "\",\"token\":\"" + tokens[client] + "\"}");
  }
  uint8_t juror() const { return game.publicView().czar; }
  int answerer(int k) const { return (juror() + 1 + k) % 4; }
  void submit(int client) {
    // The revision the phone saw last.
    const std::string frame = last(client);
    const size_t at = frame.find("\"revision\":");
    const uint32_t revision = at == std::string::npos ? 0 : std::stoul(frame.substr(at + 11));
    send(client, "{\"type\":\"submit\",\"token\":\"" + tokens[client] + "\",\"revision\":" + std::to_string(revision) +
                     ",\"slots\":[0]}");
  }
};
}  // namespace

void pauseOverTheWire() {
  Party p;
  const int a = p.answerer(0), b = p.answerer(1);
  p.ticketed(a, "pause");
  CHECK(p.server.table().pausedBy() == a);
  CHECK(p.lastHas(a, "\"pausedBy\":\"P" + std::to_string(a) + "·"));
  CHECK(p.lastHas(a, "\"pausedByMe\":true"));
  CHECK(p.lastHas(b, "\"pausedByMe\":false"));
  // Not theirs to lift; moves are refused while paused.
  p.ticketed(b, "unpause");
  CHECK(p.lastHas(b, "\"type\":\"state\"") || p.lastHas(b, "\"type\":\"error\""));
  CHECK(p.server.table().pausedBy() == a);
  p.submit(b);
  CHECK(p.game.publicView().submitted == 0);
  // The pauser may lift it; then the move goes through.
  p.ticketed(a, "unpause");
  CHECK(p.server.table().pausedBy() == kNone);
  p.submit(b);
  CHECK(p.game.publicView().submitted == 1);
  // The host may lift anyone's pause.
  p.ticketed(a, "pause");
  CHECK(p.server.hostUnpause());
  CHECK(p.lastHas(a, "\"pausedBy\":\"\""));
  CHECK(p.server.hostPause() && p.lastHas(b, "\"pausedByHost\":true"));
  p.ticketed(a, "unpause");
  CHECK(p.server.table().pausedBy() == kHost);
  // A forged or stale token cannot pause.
  p.send(a, "{\"type\":\"pause\",\"token\":\"0123456789abcdef0123456789abcdef\"}");
  CHECK(p.server.hostUnpause());
  CHECK(p.server.table().pausedBy() == kNone);
  std::puts("PASS pause: anyone sets it, only the pauser or the host lifts it, moves wait");
}

void kickOverTheWire() {
  Party p;
  const int gone = p.answerer(0);
  const std::string oldToken = p.tokens[gone];
  CHECK(p.server.hostKick(static_cast<uint8_t>(gone)));
  CHECK(p.lastHas(gone, "\"type\":\"kicked\""));
  bool closed = false;
  for (uint8_t c : p.ws->disconnected) closed = closed || c == gone;
  CHECK(closed);
  CHECK(!p.game.publicView().players[gone].seated);
  // The old ticket is dead: a reload lands on the join screen.
  p.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(gone));
  p.ws->event(WStype_CONNECTED, 9);
  p.send(9, "{\"type\":\"resume\",\"token\":\"" + oldToken + "\"}");
  CHECK(p.lastHas(9, "\"type\":\"expired\""));
  // Joining needs the next lobby.
  p.send(9, "{\"type\":\"join\",\"name\":\"Back\"}");
  CHECK(p.lastHas(9, "\"code\":\"game_running\""));
  CHECK(!p.server.hostKick(static_cast<uint8_t>(gone)));
  std::puts("PASS kick: phone told, socket closed, ticket revoked, rejoin from the next lobby");
}

void hostEndAndVotes() {
  Party p;
  const int a = p.answerer(0);
  p.ticketed(a, "again");
  CHECK(p.server.table().votes() == 0);  // only after the match
  CHECK(p.server.hostEnd());
  CHECK(p.lastHas(a, "\"phase\":\"Finished\"") && p.lastHas(a, "\"endedByHost\":true"));
  p.ticketed(a, "again");
  p.ticketed(p.answerer(1), "again");
  CHECK(p.server.table().votes() == 2);
  CHECK(p.lastHas(a, "\"votes\":2") && p.lastHas(a, "\"voted\":true"));
  CHECK(p.lastHas(p.answerer(2), "\"voted\":false"));
  CHECK(!p.server.hostEnd());
  CHECK(p.server.hostRematch());
  CHECK(p.lastHas(a, "\"phase\":\"Lobby\"") && p.lastHas(a, "\"votes\":0") && p.lastHas(a, "\"endedByHost\":false"));
  std::puts("PASS host end: final standings for everyone, again votes, host decides");
}

void clockOnTheWire() {
  Party p;
  Settings s;
  s.answerSecs = 30;
  s.extensionSecs = 10;
  p.server.hostSettings(s);
  // The clock starts with the next round; this one has none yet.
  CHECK(p.lastHas(p.answerer(0), "\"clock\":\"\""));
  CHECK(p.server.hostAbort() && p.server.hostNext());
  CHECK(p.lastHas(p.answerer(0), "\"clock\":\"answers\",\"left\":30000"));
  clockMs += 30000;
  p.server.loop();
  CHECK(p.lastHas(p.answerer(0), "\"left\":10000,\"extended\":true"));
  CHECK(p.server.hostAddTime());
  CHECK(p.lastHas(p.answerer(0), "\"left\":20000"));
  // Block: a needed phone gone for 15 s shows up by name on every phone.
  const int away = p.answerer(1);
  p.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(away));
  p.server.loop();
  clockMs += kBlockAfterMs;
  p.server.loop();
  CHECK(p.lastHas(p.answerer(0), "\"blockedOn\":\"P" + std::to_string(away) + "·"));
  CHECK(p.server.hostContinue());
  CHECK(p.lastHas(p.answerer(0), "\"blockedOn\":\"\""));
  std::puts("PASS clock fields: answers clock, extension, +time, block by name");
}

int main() {
  pauseOverTheWire();
  kickOverTheWire();
  hostEndAndVotes();
  clockOnTheWire();
  std::printf("cardparty host controls: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
