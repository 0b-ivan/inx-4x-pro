// CardPartySessions: secret-scoped multi-client command gateway and reconnect.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "CardPartySessions.h"
using namespace cardparty;

static int assertions = 0;
static int failures = 0;
#define CHECK(x) do { ++assertions; if (!(x)) { ++failures; \
  std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

struct Fixture {
  char strings[160][32] = {};
  Answer answers[160] = {};
  Prompt prompts[3] = {
      {"Why is production down? ____", 1, 0},
      {"____ plus ____", 2, 0},
      {"____ + ____ + ____", 3, 2}};
  Deck deck{};
  Fixture() {
    for (int i = 0; i < 160; ++i) {
      std::snprintf(strings[i], sizeof(strings[i]), "Sample response %d", i);
      answers[i].text = strings[i];
    }
    deck = Deck{prompts, 3, answers, 160};
  }
};

struct Key {
  uint8_t b[kTicketBytes] = {};
  explicit Key(uint8_t seed) {
    for (size_t i = 0; i < kTicketBytes; ++i)
      b[i] = static_cast<uint8_t>(seed + i + 1);
  }
};

void testJoinAndAuth() {
  Fixture f;
  Game game;
  CHECK(game.configure(f.deck, Rules{5, 2, false}, 0x10101010));
  Sessions sessions(game);
  SessionTicket a{}, b{}, c{}, rejected{};
  const Key ka(1), kb(21), kc(41), rogue(61);
  const uint8_t zero[kTicketBytes] = {};

  CHECK(!sessions.join(10, "Alice", zero, rejected));
  CHECK(!sessions.join(kNoConnection, "Alice", ka.b, rejected));
  CHECK(sessions.join(10, "Alice", ka.b, a));
  CHECK(a.seat == 0 && a.revision == 0);
  CHECK(std::memcmp(a.secret, ka.b, kTicketBytes) == 0);
  CHECK(!sessions.join(10, "Second Alice", rogue.b, rejected));
  CHECK(!sessions.join(11, "Duplicate Token", ka.b, rejected));
  CHECK(sessions.join(11, "Bob", kb.b, b));
  CHECK(sessions.join(12, "Charlie", kc.b, c));
  CHECK(a.seat != b.seat && b.seat != c.seat);
  CHECK(sessions.hostStart());

  HandView hand{};
  uint32_t revision = 99;
  CHECK(!sessions.privateHand(13, ka.b, hand, revision));
  CHECK(!sessions.privateHand(10, rogue.b, hand, revision));
  CHECK(!sessions.privateHand(11, ka.b, hand, revision));
  CHECK(sessions.privateHand(10, ka.b, hand, revision));
  CHECK(hand.judge && revision == 0);
  CHECK(sessions.privateHand(11, kb.b, hand, revision));
  CHECK(!hand.judge && revision == 0 && hand.size >= 5);

  const uint8_t slots[3] = {0, 1, 2};
  const uint8_t pick = sessions.publicView().pick;
  CHECK(!sessions.submit(10, ka.b, 0, slots, pick)); // judge cannot play
  CHECK(!sessions.submit(11, kb.b, 1, slots, pick)); // stale/newer rev
  CHECK(!sessions.submit(11, rogue.b, 0, slots, pick));
  CHECK(!sessions.submit(12, kb.b, 0, slots, pick)); // wrong connection
  CHECK(sessions.submit(11, kb.b, 0, slots, pick));
  CHECK(!sessions.submit(11, kb.b, 0, slots, pick)); // replayed revision
  CHECK(sessions.privateHand(11, kb.b, hand, revision));
  CHECK(revision == 1 && hand.submitted);
  CHECK(sessions.publicView().plays == 0); // cards hidden while collecting
  CHECK(sessions.submit(12, kc.b, 0, slots, pick));
  const PublicView judging = sessions.publicView();
  CHECK(judging.phase == Phase::Judging && judging.plays == 2);
  for (uint8_t i = 0; i < judging.plays; ++i) {
    CHECK(judging.pile[i].author == kNone);
    CHECK(judging.pile[i].cards[0] != kNoCard);
  }
  CHECK(!sessions.judge(11, kb.b, 1, 0));  // player not czar
  CHECK(!sessions.judge(10, rogue.b, 0, 0));
  CHECK(sessions.judge(10, ka.b, 0, 0));
  CHECK(!sessions.judge(10, ka.b, 0, 0)); // duplicate cannot rescore
  const auto revealed = sessions.publicView();
  CHECK(revealed.phase == Phase::Reveal);
  CHECK(revealed.winner == revealed.pile[0].author);
  CHECK(revealed.players[revealed.winner].points == 1);
  CHECK(sessions.hostNext());
  CHECK(sessions.publicView().czar == b.seat);
}

void testReconnectRotatesSecret() {
  Fixture f;
  Game game;
  CHECK(game.configure(f.deck, Rules{5, 0, true}, 0xaaaaa));
  Sessions sessions(game);
  SessionTicket a{}, b{}, c{}, renewed{};
  const Key old(70), second(90), third(110), fresh(130), attempt(150);
  CHECK(sessions.join(1, "A", old.b, a));
  CHECK(sessions.join(2, "B", second.b, b));
  CHECK(sessions.join(3, "C", third.b, c));
  CHECK(sessions.hostStart());
  CHECK(!sessions.resume(9, old.b, fresh.b, renewed)); // old connection still bound
  CHECK(sessions.disconnect(1));
  CHECK(!sessions.disconnect(1));
  CHECK(!sessions.resume(2, old.b, fresh.b, renewed)); // already bound to B
  CHECK(!sessions.resume(9, attempt.b, fresh.b, renewed)); // wrong proof
  CHECK(!sessions.resume(9, old.b, second.b, renewed)); // duplicate new token
  CHECK(sessions.resume(9, old.b, fresh.b, renewed));
  CHECK(renewed.seat == a.seat);
  CHECK(std::memcmp(renewed.secret, old.b, kTicketBytes) != 0);
  CHECK(std::memcmp(renewed.secret, fresh.b, kTicketBytes) == 0);
  CHECK(!sessions.resume(10, old.b, attempt.b, renewed)); // old revoked
  HandView view{};
  uint32_t rev = 0;
  CHECK(!sessions.privateHand(1, old.b, view, rev));
  CHECK(!sessions.privateHand(9, old.b, view, rev));
  CHECK(sessions.privateHand(9, fresh.b, view, rev));
  CHECK(view.judge); // restored original seat role
  CHECK(!sessions.disconnect(1)); // stale disconnect doesn't kill new socket
  CHECK(sessions.privateHand(9, fresh.b, view, rev));
  CHECK(!sessions.join(9, "Impersonator", attempt.b, renewed));

  // Even if a transport reuses socket IDs, token proof is mandatory.
  CHECK(sessions.disconnect(9));
  CHECK(!sessions.privateHand(9, fresh.b, view, rev));
  CHECK(!sessions.resume(2, fresh.b, attempt.b, renewed));
  CHECK(sessions.resume(9, fresh.b, attempt.b, renewed));
  CHECK(!sessions.privateHand(9, fresh.b, view, rev));
  CHECK(sessions.privateHand(9, attempt.b, view, rev));
}

void testLobbyLeaveAndReset() {
  Fixture f;
  Game game;
  CHECK(game.configure(f.deck, Rules{5, 1, false}, 88));
  Sessions sessions(game);
  const Key ka(5), kb(25), kc(45);
  SessionTicket a{}, b{}, c{};
  CHECK(sessions.join(100, "One", ka.b, a));
  CHECK(sessions.join(101, "Two", kb.b, b));
  CHECK(!sessions.leaveLobby(101, ka.b, 0));
  CHECK(sessions.leaveLobby(101, kb.b, 0));
  CHECK(!sessions.leaveLobby(101, kb.b, 0));
  CHECK(sessions.join(102, "Three", kc.b, c));
  CHECK(c.seat == b.seat);
  sessions.reset(); // must be called with reconfigure for a new session
  HandView hand{}; uint32_t rev = 0;
  CHECK(!sessions.privateHand(100, ka.b, hand, rev));
  CHECK(!sessions.privateHand(102, kc.b, hand, rev));
}

int main() {
  testJoinAndAuth();
  testReconnectRotatesSecret();
  testLobbyLeaveAndReset();
  std::printf("cardparty sessions: %d checks, %d failures\n", assertions, failures);
  return failures ? 1 : 0;
}
