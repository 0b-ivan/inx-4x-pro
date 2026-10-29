// Eight-browser load and reconnect stress against the real X4 server adapter.
//
// The fake WebSocket transport models the arduinoWebSockets properties that
// matter on the X4 Pro: WEBSOCKETS_SERVER_CLIENT_MAX=10 numbered slots, the
// lowest free slot number is reused, a full server refuses new sockets, and
// frames to a closed slot are lost. Every step re-checks seat/slot invariants.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../src/apps_local/cardparty/CardPartyDemoDeck.h"
#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"
#include "../../src/apps_local/cardparty/web/CardPartyProtocol.h"

namespace devmode {
void pause() {}
void resume() {}
}  // namespace devmode
using namespace cardparty;
using namespace cardparty::web;

namespace {
int failures = 0, checks = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    ++checks;                                                              \
    if (!(cond)) {                                                         \
      ++failures;                                                          \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                      \
  } while (0)

constexpr int kSlots = 10;  // platformio.ini: -DWEBSOCKETS_SERVER_CLIENT_MAX=10

uint32_t rng = 1;
uint32_t roll(uint32_t n) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng % n;
}

struct Player {
  std::string name, token;
  int slot = -1;  // -1: browser currently has no socket
  uint32_t revision = 0;
};

struct Harness {
  Game game;
  Sessions sessions{game};
  Server server{game, sessions, demoDeck()};
  WebSocketsServer* ws = nullptr;
  std::vector<Player> players;
  std::map<std::string, int> tokenOwner;  // every token ever issued -> slot
  size_t maxFrame = 0;
  size_t scanned = 0;

  bool begin(uint32_t seed, uint8_t rounds) {
    if (!game.configure(demoDeck(), Rules{5, rounds, true}, seed)) return false;
    sessions.reset();
    if (!server.begin(Server::Mode::Hotspot)) return false;
    ws = WebSocketsServer::instance;
    ws->slotLimit = kSlots;
    ws->strictDelivery = true;
    ws->messages.clear();
    ws->recipients.clear();
    scanned = 0;
    return true;
  }
  void text(int slot, const std::string& json) {
    ws->event(WStype_TEXT, static_cast<uint8_t>(slot), json);
    audit();
  }
  // Tokens may only ever travel to the socket they were issued on.
  void audit() {
    const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
    for (; scanned < ws->messages.size(); ++scanned) {
      const std::string& m = ws->messages[scanned];
      const int to = ws->recipients[scanned];
      if (m.size() > maxFrame) maxFrame = m.size();
      if (m.rfind(prefix, 0) == 0) {
        const std::string t = m.substr(prefix.size(), 32);
        CHECK(!tokenOwner.count(t));
        tokenOwner[t] = to;
      }
      for (const auto& [t, owner] : tokenOwner)
        if (m.find(t) != std::string::npos) CHECK(owner == to);
    }
  }
  std::string lastTo(int slot, size_t from) const {
    for (size_t i = ws->messages.size(); i-- > from;)
      if (ws->recipients[i] == slot) return ws->messages[i];
    return {};
  }
  static uint32_t revisionIn(const std::string& m) {
    const auto at = m.find("\"revision\":");
    return at == std::string::npos ? 0 : static_cast<uint32_t>(std::atoi(m.c_str() + at + 11));
  }
  static std::string handIn(const std::string& m) {
    const auto at = m.find("\"hand\":[");
    return at == std::string::npos ? std::string() : m.substr(at, m.find(']', at) - at);
  }
  void track(Player& p, size_t from) {
    const std::string m = lastTo(p.slot, from);
    if (m.find("\"private\"") != std::string::npos) p.revision = revisionIn(m);
  }
  void refreshRevisions(size_t from) {
    for (auto& p : players)
      if (p.slot >= 0) track(p, from);
  }

  bool join(const std::string& name) {
    const int slot = ws->accept();
    if (slot < 0) return false;
    const size_t mark = ws->messages.size();
    std::string escaped;
    for (char c : name) {
      if (c == '"' || c == '\\') escaped += '\\';
      escaped += c;
    }
    text(slot, "{\"type\":\"join\",\"name\":\"" + escaped + "\"}");
    const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
    for (size_t i = mark; i < ws->messages.size(); ++i)
      if (ws->recipients[i] == slot && ws->messages[i].rfind(prefix, 0) == 0) {
        Player p;
        p.name = name;
        p.slot = slot;
        p.token = ws->messages[i].substr(prefix.size(), 32);
        players.push_back(p);
        refreshRevisions(mark);
        return true;
      }
    return false;
  }
  int seatOf(const Player& p) const {
    const auto v = game.publicView();
    for (int i = 0; i < kPlayers; ++i)
      if (v.players[i].seated && p.name == v.players[i].name) return i;
    return -1;
  }
  // Page reload: the new socket opens (and resumes) before the old one has
  // been noticed as closed. `closeFirst` models a clean close instead.
  bool reload(Player& p, bool closeFirst) {
    const int old = p.slot;
    if (closeFirst && old >= 0) {
      ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(old));
      p.slot = -1;
    }
    const std::string handBefore = old >= 0 ? handIn(lastTo(old, 0)) : std::string();
    const int slot = ws->accept();
    if (slot < 0) return false;
    const size_t mark = ws->messages.size();
    text(slot, "{\"type\":\"resume\",\"token\":\"" + p.token + "\"}");
    const std::string prefix = "{\"type\":\"welcome\",\"token\":\"";
    std::string welcome;
    for (size_t i = mark; i < ws->messages.size() && welcome.empty(); ++i)
      if (ws->recipients[i] == slot && ws->messages[i].rfind(prefix, 0) == 0) welcome = ws->messages[i];
    if (welcome.empty()) return false;
    const std::string fresh = welcome.substr(prefix.size(), 32);
    CHECK(fresh != p.token);
    if (!closeFirst && old >= 0 && old != slot) CHECK(!ws->open.count(static_cast<uint8_t>(old)));
    if (!handBefore.empty()) CHECK(handIn(welcome) == handBefore);  // hand survives
    const std::string stale = p.token;
    p.token = fresh;
    p.slot = slot;
    p.revision = revisionIn(welcome);
    // The revoked ticket cannot act through the replacement socket.
    const size_t probe = ws->messages.size();
    text(slot, "{\"type\":\"judge\",\"token\":\"" + stale + "\",\"revision\":" + std::to_string(p.revision) +
                   ",\"selected\":0}");
    CHECK(ws->messages.size() == probe + 1);
    CHECK(ws->messages.back().find("\"type\":\"error\"") != std::string::npos);
    return true;
  }
  bool submit(Player& p) {
    const auto v = game.publicView();
    std::string slots;
    for (int i = 0; i < v.pick; ++i) slots += (i ? "," : "") + std::to_string(i);
    const uint16_t before = v.submitted;
    const size_t mark = ws->messages.size();
    text(p.slot, "{\"type\":\"submit\",\"token\":\"" + p.token + "\",\"revision\":" + std::to_string(p.revision) +
                     ",\"slots\":[" + slots + "]}");
    refreshRevisions(mark);
    return game.publicView().submitted == before + 1 || game.phase() != Phase::Playing;
  }
  bool judge(Player& p, uint8_t chosen) {
    const size_t mark = ws->messages.size();
    text(p.slot, "{\"type\":\"judge\",\"token\":\"" + p.token + "\",\"revision\":" + std::to_string(p.revision) +
                     ",\"selected\":" + std::to_string(chosen) + "}");
    refreshRevisions(mark);
    return game.phase() == Phase::Reveal;
  }
  Player* czar() {
    const auto v = game.publicView();
    for (auto& p : players)
      if (seatOf(p) == v.czar) return &p;
    return nullptr;
  }
  // Global invariants after every step.
  void invariants() {
    std::set<int> bound;
    const auto v = game.publicView();
    for (const auto& p : players) {
      const int seat = seatOf(p);
      CHECK(seat >= 0);
      if (p.slot < 0) continue;
      CHECK(ws->open.count(static_cast<uint8_t>(p.slot)));
      CHECK(!bound.count(p.slot));  // one socket never serves two seats
      bound.insert(p.slot);
      CHECK(sessions.seatOf(static_cast<uint32_t>(p.slot)) == seat);
      CHECK(v.players[seat].connected);
    }
    CHECK(static_cast<int>(ws->open.size()) <= kSlots);
    uint32_t points = 0;
    for (const auto& pl : v.players) points += pl.points;
    CHECK(points <= v.round);  // at most one human point per round
  }
};

// Eight browsers, ten rounds, with reloads in every phase.
void eightPlayerStorm(uint32_t seed) {
  rng = seed;
  Harness h;
  CHECK(h.begin(seed, 10));
  const char* names[8] = {"ADA",
                          "J\xc3\xbcrgen-Sch\xc3\xa4"
                          "fer",
                          "\"QUOTE\"",
                          "N\xc3\xb6"
                          "de",
                          "Eve",
                          "Mallory",
                          "Trent",
                          "Zo\xc3\xab"};
  for (const char* n : names) CHECK(h.join(n));
  CHECK(h.players.size() == 8);
  h.invariants();

  // Two extra tabs: one idle, one rejected ninth player. Both are evicted
  // after the authentication grace window, freeing their slots.
  const int idle = h.ws->accept();
  const int ninth = h.ws->accept();
  CHECK(idle >= 0 && ninth >= 0 && h.ws->open.size() == 10);
  CHECK(h.ws->accept() == -1);  // all ten library slots are in use
  const size_t mark = h.ws->messages.size();
  h.text(ninth, R"({"type":"join","name":"Ninth"})");
  CHECK(h.lastTo(ninth, mark).find("\"code\":\"session_full\"") != std::string::npos);
  clockMs += Server::kAuthGraceMs - 1;
  h.server.loop();
  CHECK(h.ws->open.size() == 10);
  clockMs += 2;
  h.server.loop();
  CHECK(h.ws->open.size() == 8);
  CHECK(!h.ws->open.count(static_cast<uint8_t>(idle)) && !h.ws->open.count(static_cast<uint8_t>(ninth)));
  CHECK(h.server.diagnostics().idleClosed == 2);
  h.invariants();

  // Reload storm in the lobby; slot numbers get reused across players.
  for (int i = 0; i < 40; ++i) {
    CHECK(h.reload(h.players[roll(8)], roll(2) == 0));
    h.invariants();
  }
  CHECK(h.ws->open.size() == 8);
  CHECK(h.server.hostStart());
  h.refreshRevisions(0);

  uint32_t judged = 0;
  for (int round = 0; round < h.game.rules().rounds; ++round) {
    CHECK(h.game.phase() == Phase::Playing);
    Player* czar = h.czar();
    CHECK(czar != nullptr);
    // Playing: everyone submits; some reload mid-selection, some right after.
    for (auto& p : h.players) {
      if (&p == czar) continue;
      if (roll(4) == 0) CHECK(h.reload(p, roll(2) == 0));
      const Phase before = h.game.phase();
      CHECK(before == Phase::Playing);
      const bool last = h.game.publicView().submitted + 1 == h.game.publicView().expected;
      CHECK(h.submit(p));
      if (!last) CHECK(h.game.phase() == Phase::Playing);  // no premature judging
      if (roll(5) == 0) CHECK(h.reload(p, roll(2) == 0));
      // A duplicate submit after reload is rejected and does not count twice.
      if (h.game.phase() == Phase::Playing) {
        const uint16_t n = h.game.publicView().submitted;
        h.submit(p);
        CHECK(h.game.publicView().submitted == n);
      }
      h.invariants();
    }
    CHECK(h.game.phase() == Phase::Judging);
    CHECK(h.game.publicView().plays == 8);  // 7 humans + Rando
    // Judging: the Czar reloads, then votes once; a replay is rejected.
    if (roll(2) == 0) CHECK(h.reload(*czar, roll(2) == 0));
    uint32_t pointsBefore = 0;
    for (const auto& pl : h.game.publicView().players) pointsBefore += pl.points;
    CHECK(h.judge(*czar, static_cast<uint8_t>(roll(8))));
    ++judged;
    uint32_t pointsAfter = 0;
    for (const auto& pl : h.game.publicView().players) pointsAfter += pl.points;
    const bool rando = h.game.publicView().winner == kRando;
    CHECK(pointsAfter == pointsBefore + (rando ? 0u : 1u));
    const size_t replayAt = h.ws->messages.size();
    h.text(czar->slot, "{\"type\":\"judge\",\"token\":\"" + czar->token +
                           "\",\"revision\":" + std::to_string(czar->revision - 1) + ",\"selected\":0}");
    CHECK(h.ws->messages[replayAt].find("\"type\":\"error\"") != std::string::npos);
    uint32_t pointsReplay = 0;
    for (const auto& pl : h.game.publicView().players) pointsReplay += pl.points;
    CHECK(pointsReplay == pointsAfter);
    // Reveal: reloads keep scores.
    CHECK(h.reload(h.players[roll(8)], roll(2) == 0));
    h.invariants();
    CHECK(h.server.hostNext());
    h.refreshRevisions(0);
  }
  CHECK(h.game.phase() == Phase::Finished);
  CHECK(judged == 8 && h.game.rules().rounds == 8);  // ten requested, eight at the table
  // Late joiner during a running/finished game is refused with a reason.
  const int late = h.ws->accept();
  CHECK(late >= 0);
  const size_t lateAt = h.ws->messages.size();
  h.text(late, R"({"type":"join","name":"Late"})");
  CHECK(h.lastTo(late, lateAt).find("\"code\":\"game_running\"") != std::string::npos);
  h.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(late));

  // Rematch keeps network, seats and tickets; scores reset; reloads still work.
  CHECK(h.server.hostRematch());
  CHECK(h.game.phase() == Phase::Lobby);
  for (const auto& pl : h.game.publicView().players) CHECK(pl.points == 0);
  for (auto& p : h.players) CHECK(h.lastTo(p.slot, 0).find("\"phase\":\"Lobby\"") != std::string::npos);
  for (int i = 0; i < 8; ++i) CHECK(h.reload(h.players[roll(8)], roll(2) == 0));
  CHECK(h.server.hostStart());
  CHECK(h.game.publicView().round == 1);
  h.invariants();
  CHECK(h.ws->open.size() == 8);

  // Bounded outgoing buffer: worst demo-deck frame keeps a wide margin.
  CHECK(h.maxFrame > 0 && h.maxFrame < 4096);
  CHECK(h.server.diagnostics().sendFailures == 0);
  const auto diag = h.server.diagnostics();
  CHECK(diag.takeovers > 0);
  h.server.stop();
  std::printf(
      "PASS 8 browsers seed=%u: 8 fair rounds (10 requested), reloads in Lobby/Playing/Judging/Reveal, "
      "rematch, max frame %zu bytes, takeovers %u, rejected %u\n",
      seed, h.maxFrame, 0u + diag.takeovers, 0u + diag.rejected);
}

// A lobby seat whose browser is gone is reclaimed only when the lobby is full.
void ghostSeatReclaim() {
  Harness h;
  CHECK(h.begin(7, 3));
  for (int i = 0; i < 8; ++i) CHECK(h.join(std::string("P") + char('A' + i)));
  Player ghost = h.players[3];
  h.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(ghost.slot));
  CHECK(h.game.lobbyFull());
  CHECK(h.join("Newcomer"));  // takes the disconnected seat
  CHECK(h.game.publicView().players[3].seated);
  CHECK(std::strcmp(h.game.publicView().players[3].name, "Newcomer") == 0);
  // The reclaimed seat's old ticket is now expired.
  const int slot = h.ws->accept();
  const size_t mark = h.ws->messages.size();
  h.text(slot, "{\"type\":\"resume\",\"token\":\"" + ghost.token + "\"}");
  CHECK(h.lastTo(slot, mark) == R"({"type":"expired"})");
  // With every seat live, a further join is refused and nobody is displaced.
  const size_t fullAt = h.ws->messages.size();
  h.text(slot, R"({"type":"join","name":"Ninth"})");
  CHECK(h.lastTo(slot, fullAt).find("\"code\":\"session_full\"") != std::string::npos);
  for (int i = 0; i < 8; ++i) CHECK(h.game.publicView().players[i].connected);
  // A 24-byte UTF-8 name exceeds the 23-byte seat name and is refused
  // without evicting anyone.
  h.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(h.players[0].slot));
  const size_t longAt = h.ws->messages.size();
  h.text(slot,
         "{\"type\":\"join\",\"name\":\"J\xc3\xbcrgen-Sch\xc3\xa4"
         "fer-M\xc3\xbcller\"}");
  CHECK(h.lastTo(slot, longAt).find("\"type\":\"error\"") != std::string::npos);
  CHECK(h.game.publicView().players[0].seated);
  h.server.stop();
  std::puts("PASS ghost lobby seat reclaimed only when full; live seats never displaced");
}

// The last pending answerer locks the phone: the round waits, blocks after
// 15 s until the host goes on without them; returning in time keeps the turn.
void heldTurnGrace() {
  Harness h;
  CHECK(h.begin(11, 3));
  for (int i = 0; i < 4; ++i) CHECK(h.join(std::string("H") + char('A' + i)));
  CHECK(h.server.hostStart());
  h.refreshRevisions(0);
  Player* czar = h.czar();
  std::vector<Player*> answer;
  for (auto& p : h.players)
    if (&p != czar) answer.push_back(&p);
  CHECK(h.submit(*answer[0]));
  CHECK(h.submit(*answer[1]));
  // Phone lock: socket drops cleanly, browser not back yet.
  h.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(answer[2]->slot));
  answer[2]->slot = -1;
  h.server.loop();
  CHECK(h.game.phase() == Phase::Playing);
  clockMs += kBlockAfterMs - 1;
  h.server.loop();
  CHECK(h.game.phase() == Phase::Playing);
  CHECK(h.server.table().blockedOn() == kNone);
  // Back within the window: resumes and answers normally.
  CHECK(h.reload(*answer[2], false));
  CHECK(h.submit(*answer[2]));
  CHECK(h.game.phase() == Phase::Judging);
  CHECK(h.judge(*h.czar(), 0));
  CHECK(h.server.hostNext());
  h.refreshRevisions(0);
  // Next round: the pending player stays away past the window.
  czar = h.czar();
  answer.clear();
  for (auto& p : h.players)
    if (&p != czar) answer.push_back(&p);
  CHECK(h.submit(*answer[0]));
  CHECK(h.submit(*answer[1]));
  h.ws->event(WStype_DISCONNECTED, static_cast<uint8_t>(answer[2]->slot));
  answer[2]->slot = -1;
  h.server.loop();
  clockMs += kBlockAfterMs;
  h.server.loop();
  // Away past 15 s: the table waits for the host instead of moving on.
  CHECK(h.game.phase() == Phase::Playing);
  CHECK(h.server.table().blockedOn() != kNone);
  CHECK(h.server.hostContinue());
  CHECK(h.game.phase() == Phase::Judging);
  CHECK(h.game.publicView().plays == 3);  // two answers + Rando
  h.server.stop();
  std::puts("PASS held turn: waits through a phone lock, blocks after 15 s, host goes on without them");
}

// A browser re-proving its own seat on the same socket rotates, not expires.
void resumeOnOwnSocket() {
  Harness h;
  CHECK(h.begin(3, 3));
  for (int i = 0; i < 3; ++i) CHECK(h.join(std::string("S") + char('A' + i)));
  Player& p = h.players[1];
  const size_t mark = h.ws->messages.size();
  h.text(p.slot, "{\"type\":\"resume\",\"token\":\"" + p.token + "\"}");
  CHECK(h.ws->messages.size() > mark);
  CHECK(h.ws->messages[mark].rfind("{\"type\":\"welcome\"", 0) == 0);
  CHECK(h.ws->recipients[mark] == p.slot);
  p.token = h.ws->messages[mark].substr(27, 32);
  // But one socket can never take over a second seat.
  const size_t crossAt = h.ws->messages.size();
  h.text(p.slot, "{\"type\":\"resume\",\"token\":\"" + h.players[2].token + "\"}");
  CHECK(h.lastTo(p.slot, crossAt) == R"({"type":"expired"})");
  CHECK(h.sessions.seatOf(static_cast<uint32_t>(h.players[2].slot)) == 2);
  h.server.stop();
  std::puts("PASS resume on own socket rotates ticket; cross-seat resume refused");
}
}  // namespace

int main() {
  for (uint32_t seed : {1u, 89u, 20261027u}) eightPlayerStorm(seed);
  ghostSeatReclaim();
  heldTurnGrace();
  resumeOnOwnSocket();
  std::printf("cardparty stress: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
