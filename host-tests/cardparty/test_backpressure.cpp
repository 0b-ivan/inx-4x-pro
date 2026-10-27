// TCP back-pressure: one phone that stops reading must not stall the X4 loop.
//
// On the X4 every write to a socket with a full send buffer (state frame,
// ping, close frame) blocks for seconds. The fake marks such sockets as
// stalled and counts any write to them as a blocking write; the server must
// make none, keep serving the other phones, and drop the dead one.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../../src/apps_local/cardparty/CardPartyDemoDeck.h"
#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"
#include "../../src/apps_local/cardparty/web/CardPartyProtocol.h"

namespace devmode { void pause() {} void resume() {} }
using namespace cardparty;
using namespace cardparty::web;

namespace {
int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
  std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const std::string kWelcome = "{\"type\":\"welcome\",\"token\":\"";

struct Phone {
  std::string name, token;
  int slot = -1;
  uint32_t revision = 0;
};

struct Table {
  Game game;
  Sessions sessions{game};
  Server server{game, sessions, demoDeck()};
  WebSocketsServer* ws = nullptr;
  std::vector<Phone> phones;

  explicit Table(int players) {
    clockMs = 1000;
    CHECK(game.configure(demoDeck(), Rules{5, 10, true}, 7));
    CHECK(server.begin(Server::Mode::Hotspot));
    ws = WebSocketsServer::instance;
    ws->slotLimit = 10;
    ws->strictDelivery = true;
    static const char* names[] = {"ADA", "BERT", "CYRA", "DORO", "EMIL", "FINN", "GUS", "HEDI"};
    for (int i = 0; i < players; ++i) CHECK(join(names[i]));
  }
  void text(int slot, const std::string& json) { ws->event(WStype_TEXT, static_cast<uint8_t>(slot), json); }
  std::string lastTo(int slot, size_t from) const {
    for (size_t i = ws->messages.size(); i-- > from;)
      if (ws->recipients[i] == slot) return ws->messages[i];
    return {};
  }
  size_t framesTo(int slot, size_t from) const {
    size_t n = 0;
    for (size_t i = from; i < ws->messages.size(); ++i) n += ws->recipients[i] == slot;
    return n;
  }
  static uint32_t revisionIn(const std::string& m) {
    const auto at = m.find("\"revision\":");
    return at == std::string::npos ? 0 : static_cast<uint32_t>(std::atoi(m.c_str() + at + 11));
  }
  void refresh(size_t from) {
    for (auto& p : phones) {
      const std::string m = lastTo(p.slot, from);
      if (m.find("\"private\"") != std::string::npos) p.revision = revisionIn(m);
    }
  }
  bool join(const char* name) {
    const int slot = ws->accept();
    if (slot < 0) return false;
    const size_t mark = ws->messages.size();
    text(slot, std::string("{\"type\":\"join\",\"name\":\"") + name + "\"}");
    for (size_t i = mark; i < ws->messages.size(); ++i)
      if (ws->recipients[i] == slot && ws->messages[i].rfind(kWelcome, 0) == 0) {
        Phone p; p.name = name; p.slot = slot;
        p.token = ws->messages[i].substr(kWelcome.size(), 32);
        phones.push_back(p);
        refresh(mark);
        return true;
      }
    return false;
  }
  int seatOf(const Phone& p) const {
    const auto v = game.publicView();
    for (int i = 0; i < kPlayers; ++i)
      if (v.players[i].seated && p.name == v.players[i].name) return i;
    return -1;
  }
  bool connected(const Phone& p) const { return game.publicView().players[seatOf(p)].connected; }
  Phone* nonCzar(int skip) {
    const auto v = game.publicView();
    for (auto& p : phones)
      if (seatOf(p) != v.czar && skip-- == 0) return &p;
    return nullptr;
  }
  void submit(Phone& p) {
    const auto v = game.publicView();
    std::string slots;
    for (int i = 0; i < v.pick; ++i) slots += (i ? "," : "") + std::to_string(i);
    const size_t mark = ws->messages.size();
    text(p.slot, "{\"type\":\"submit\",\"token\":\"" + p.token + "\",\"revision\":" +
                 std::to_string(p.revision) + ",\"slots\":[" + slots + "]}");
    refresh(mark);
  }
  // One X4 main-loop pass per simulated 250 ms.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 250) { clockMs += 250; server.loop(); }
  }
  bool wasDropped(int slot) const {
    for (uint8_t s : ws->dropped) if (s == slot) return true;
    return false;
  }
  bool wasClosed(int slot) const {
    for (uint8_t s : ws->disconnected) if (s == slot) return true;
    return false;
  }
};

// T8: airplane mode mid-round. The page stays open, the socket neither reads
// nor answers pings, and its send buffer is full.
void halfDeadPhone() {
  Table t(8);
  CHECK(t.server.hostStart());
  Phone& dead = *t.nonCzar(0);
  t.ws->stalled.insert(static_cast<uint8_t>(dead.slot));
  t.ws->silent.insert(static_cast<uint8_t>(dead.slot));

  const size_t mark = t.ws->messages.size();
  for (int i = 1; i < 4; ++i) t.submit(*t.nonCzar(i));
  CHECK(t.ws->blockingWrites == 0);
  CHECK(t.game.publicView().submitted == 3);
  for (const auto& p : t.phones)  // everyone else saw every update
    if (p.slot != dead.slot) CHECK(t.lastTo(p.slot, mark).find("\"submitted\":3") != std::string::npos);
  CHECK(t.framesTo(dead.slot, mark) == 0);
  CHECK(t.server.diagnostics().deferred > 0);

  t.run(Server::kStallDropMs + Server::kPingMs);
  CHECK(t.wasDropped(dead.slot));
  CHECK(!t.wasClosed(dead.slot));  // no close frame into a full buffer
  CHECK(t.ws->blockingWrites == 0);
  CHECK(!t.connected(dead));
  CHECK(t.server.diagnostics().dropped == 1);
  for (const auto& p : t.phones)
    if (p.slot != dead.slot) CHECK(t.connected(p) && t.ws->open.count(static_cast<uint8_t>(p.slot)));
  std::printf("PASS half-dead phone: no blocking write, others served, dropped without close frame\n");
}

// A slow phone gets the newest state once its buffer drains, not a backlog.
void slowPhoneCatchesUp() {
  Table t(5);
  CHECK(t.server.hostStart());
  Phone& slow = *t.nonCzar(0);
  t.ws->stalled.insert(static_cast<uint8_t>(slow.slot));
  const size_t mark = t.ws->messages.size();
  t.submit(*t.nonCzar(1));
  t.submit(*t.nonCzar(2));
  t.run(1000);
  CHECK(t.framesTo(slow.slot, mark) == 0);
  CHECK(t.ws->blockingWrites == 0);

  t.ws->stalled.erase(static_cast<uint8_t>(slow.slot));
  const size_t drained = t.ws->messages.size();
  t.run(250);
  CHECK(t.framesTo(slow.slot, drained) == 1);
  CHECK(t.lastTo(slow.slot, drained).find("\"submitted\":2") != std::string::npos);
  t.run(1000);
  CHECK(t.framesTo(slow.slot, drained) == 1);  // nothing left pending
  CHECK(t.connected(slow));
  std::printf("PASS slow phone: one coalesced state frame after draining, seat kept\n");
}

// Buffer empty but the phone is gone: missed pings drop it.
void silentPhone() {
  Table t(3);
  Phone& gone = t.phones[1];
  t.ws->silent.insert(static_cast<uint8_t>(gone.slot));
  t.run(Server::kPingMs * (Server::kMissedPings + 1) + 250);
  CHECK(t.wasDropped(gone.slot));
  CHECK(!t.connected(gone));
  CHECK(t.connected(t.phones[0]) && t.connected(t.phones[2]));
  CHECK(t.ws->blockingWrites == 0);
  std::printf("PASS silent phone: dropped after %u missed pings\n", Server::kMissedPings);
}

// Healthy phones answer every ping and are never dropped.
void healthyTableStays() {
  Table t(8);
  CHECK(t.server.hostStart());
  t.run(120000);
  CHECK(t.ws->dropped.empty());
  CHECK(t.ws->pinged.size() >= 8 * (120000 / Server::kPingMs) - 8);
  for (const auto& p : t.phones) CHECK(t.connected(p));
  std::printf("PASS healthy table: two minutes of heartbeats, no drops\n");
}

// Reload while the old socket is stuck: takeover must not write a close frame.
void takeoverFromStalledSocket() {
  Table t(4);
  Phone& p = t.phones[2];
  const int old = p.slot;
  t.ws->stalled.insert(static_cast<uint8_t>(old));
  const int fresh = t.ws->accept();
  const size_t mark = t.ws->messages.size();
  t.text(fresh, "{\"type\":\"resume\",\"token\":\"" + p.token + "\"}");
  bool welcomed = false;
  for (size_t i = mark; i < t.ws->messages.size(); ++i)
    welcomed |= t.ws->recipients[i] == fresh && t.ws->messages[i].rfind(kWelcome, 0) == 0;
  CHECK(welcomed);
  CHECK(t.wasDropped(old));
  CHECK(!t.wasClosed(old));
  CHECK(t.ws->blockingWrites == 0);
  p.slot = fresh;
  CHECK(t.connected(p));
  std::printf("PASS takeover from a stalled socket drops it without a close frame\n");
}

// A welcome (it carries the new ticket) is never lost to back-pressure.
void deferredWelcome() {
  Table t(3);
  const int slot = t.ws->accept();
  t.ws->stalled.insert(static_cast<uint8_t>(slot));
  const size_t mark = t.ws->messages.size();
  t.text(slot, "{\"type\":\"join\",\"name\":\"ZOE\"}");
  CHECK(t.framesTo(slot, mark) == 0);
  CHECK(t.ws->blockingWrites == 0);
  t.ws->stalled.erase(static_cast<uint8_t>(slot));
  t.run(250);
  CHECK(t.framesTo(slot, mark) == 1);
  CHECK(t.lastTo(slot, mark).rfind(kWelcome, 0) == 0);
  std::printf("PASS deferred welcome delivered once the socket drains\n");
}

// The socket drains and another player's move publishes before the next
// loop pass: the plain state frame must still carry the held-back ticket.
void deferredWelcomeRacesPublish() {
  Table t(3);
  const int slot = t.ws->accept();
  t.ws->stalled.insert(static_cast<uint8_t>(slot));
  const size_t mark = t.ws->messages.size();
  t.text(slot, "{\"type\":\"join\",\"name\":\"ZOE\"}");
  t.ws->stalled.erase(static_cast<uint8_t>(slot));
  CHECK(t.join("YANN"));  // publishAll() before any loop()
  t.run(250);
  bool welcomed = false;
  for (size_t i = mark; i < t.ws->messages.size(); ++i)
    welcomed |= t.ws->recipients[i] == slot && t.ws->messages[i].rfind(kWelcome, 0) == 0;
  CHECK(welcomed);
  std::printf("PASS deferred welcome survives a publish racing the drain\n");
}

// A stuck socket that never joined is released without a blocking write.
void stalledIdleSocket() {
  Table t(3);
  const int slot = t.ws->accept();
  t.ws->stalled.insert(static_cast<uint8_t>(slot));
  t.ws->silent.insert(static_cast<uint8_t>(slot));
  t.run(Server::kAuthGraceMs + Server::kPingMs);
  CHECK(t.wasDropped(slot));
  CHECK(!t.ws->open.count(static_cast<uint8_t>(slot)));
  CHECK(t.ws->blockingWrites == 0);
  std::printf("PASS stalled idle socket released without blocking\n");
}
}  // namespace

int main() {
  halfDeadPhone();
  slowPhoneCatchesUp();
  silentPhone();
  healthyTableStays();
  takeoverFromStalledSocket();
  deferredWelcome();
  deferredWelcomeRacesPublish();
  stalledIdleSocket();
  std::printf("cardparty back-pressure: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
