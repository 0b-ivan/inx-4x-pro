// Cards Against Whatever - host-only rules and secrecy regression suite
#include <cstdio>

#include "CardPartyCore.h"
using namespace cardparty;
static int checks = 0, failures = 0;
#define CHECK(x)                                                \
  do {                                                          \
    ++checks;                                                   \
    if (!(x)) {                                                 \
      ++failures;                                               \
      std::printf("FAILED %s:%d %s\n", __FILE__, __LINE__, #x); \
    }                                                           \
  } while (0)
struct Fixture {
  char txt[160][24] = {};
  Answer white[160] = {};
  Prompt black[3] = {{"Server outage: ____.", 1, 0}, {"____ + ____", 2, 0}, {"____, ____ and ____", 3, 2}};
  Deck deck{};
  Fixture() {
    for (int i = 0; i < 160; ++i) {
      std::snprintf(txt[i], 24, "Example answer %d", i);
      white[i].text = txt[i];
    }
    deck = Deck{black, 3, white, 160};
  }
};
void validation() {
  Fixture f;
  Game g;
  CHECK(!g.start());
  CHECK(!g.configure(Deck{}, Rules{}, 5));
  Prompt bad[] = {{"bad", 0, 0}};
  CHECK(!g.configure(Deck{bad, 1, f.white, 160}, Rules{}, 5));
  CHECK(g.configure(f.deck, Rules{5, 2, false}, 5));
  CHECK(g.join("") == kNone);
  CHECK(g.join("0123456789012345678901234") == kNone);
  auto a = g.join("Alice");
  CHECK(a == 0);
  CHECK(!g.start());
  auto b = g.join("Bob");
  CHECK(b == 1);
  CHECK(!g.start());
  CHECK(g.join("Charlie") == 2);
  CHECK(g.leaveLobby(1));
  CHECK(g.join("Bob") == b);
  CHECK(g.start());
  CHECK(g.join("late") == kNone);
}
void secretAndRound() {
  Fixture f;
  Game g;
  CHECK(g.configure(f.deck, Rules{10, 2, false}, 9));
  auto a = g.join("Alice"), b = g.join("Bob"), c = g.join("Carl");
  CHECK(g.start());
  auto p = g.publicView();
  CHECK(p.czar == a);
  CHECK(p.expected == 2);
  CHECK(p.plays == 0);
  HandView h{};
  CHECK(g.privateHand(b, h));
  CHECK(h.size >= 10);
  CHECK(!g.privateHand(150, h));
  const uint8_t slots[] = {0, 1, 2};
  CHECK(!g.submit(a, slots, p.pick));
  CHECK(!g.submit(b, slots, 0));
  CHECK(g.submit(b, slots, p.pick));
  CHECK(!g.submit(b, slots, p.pick));
  CHECK(g.publicView().plays == 0);
  CHECK(g.submit(c, slots, p.pick));
  p = g.publicView();
  CHECK(p.phase == Phase::Judging);
  CHECK(p.plays == 2);
  for (uint8_t i = 0; i < p.plays; ++i) CHECK(p.pile[i].author == kNone);
  CHECK(!g.judge(b, 0));
  CHECK(!g.judge(a, 2));
  CHECK(g.judge(a, 0));
  p = g.publicView();
  CHECK(p.phase == Phase::Reveal);
  CHECK(p.winner == p.pile[0].author);
  CHECK(p.players[p.winner].points == 1);
  CHECK(g.nextRound());
  CHECK(g.publicView().czar == b);
  CHECK(g.submit(a, slots, g.publicView().pick));
  CHECK(g.submit(c, slots, g.publicView().pick));
  CHECK(g.judge(b, 1));
  CHECK(g.nextRound());
  CHECK(g.phase() == Phase::Finished);
}
void pickAndRando() {
  Fixture f;
  Game g;
  CHECK(g.configure(f.deck, Rules{10, 0, true}, 123456));
  uint8_t players[] = {g.join("A"), g.join("B"), g.join("C"), g.join("D")};
  CHECK(g.start());
  bool saw2 = false, saw3 = false, botWon = false;
  const uint8_t slots[] = {0, 1, 2}, duplicate[] = {0, 0, 0};
  for (int turn = 0; turn < 100; ++turn) {
    auto v = g.publicView();
    CHECK(v.phase == Phase::Playing);
    if (v.pick == 2) saw2 = true;
    if (v.pick == 3) {
      saw3 = true;
      CHECK(v.draw == 2);
    }
    for (auto seat : players) {
      HandView h{};
      CHECK(g.privateHand(seat, h));
      CHECK(h.size <= 12);
      for (uint8_t i = 0; i < h.size; ++i)
        for (uint8_t j = i + 1; j < h.size; ++j) CHECK(h.cards[i] != h.cards[j]);
      if (seat == v.czar) continue;
      if (v.pick > 1) CHECK(!g.submit(seat, duplicate, v.pick));
      CHECK(g.submit(seat, slots, v.pick));
    }
    auto j = g.publicView();
    CHECK(j.phase == Phase::Judging);
    CHECK(j.plays == 4);
    for (uint8_t i = 0; i < j.plays; ++i) {
      CHECK(j.pile[i].author == kNone);
      for (uint8_t x = 0; x < v.pick; ++x)
        for (uint8_t y = x + 1; y < v.pick; ++y) CHECK(j.pile[i].cards[x] != j.pile[i].cards[y]);
    }
    CHECK(g.judge(v.czar, static_cast<uint8_t>(turn % j.plays)));
    auto r = g.publicView();
    CHECK(r.winner == r.pile[r.selected].author);
    if (r.winner == kRando) botWon = true;
    CHECK(g.nextRound());
  }
  CHECK(saw2);
  CHECK(saw3);
  CHECK(botWon);
}
void disconnectAndBounds() {
  Fixture f;
  Game g;
  CHECK(g.configure(f.deck, Rules{5, 0, false}, 5));
  auto a = g.join("A"), b = g.join("B"), c = g.join("C");
  CHECK(g.start());
  const uint8_t slots[] = {0, 1, 2};
  CHECK(!g.closeSubmissions());
  CHECK(g.submit(b, slots, g.publicView().pick));
  CHECK(g.connected(c, false));
  CHECK(g.phase() == Phase::Playing);
  CHECK(g.held(c));
  CHECK(g.releaseTurn(c));
  CHECK(g.phase() == Phase::Judging);
  CHECK(!g.submit(c, slots, g.publicView().pick));
  CHECK(g.connected(c, true));
  CHECK(g.judge(a, 0));
  CHECK(g.nextRound());
  CHECK(g.publicView().czar == b);
  CHECK(g.abortRound());
  CHECK(g.publicView().aborted);
  CHECK(g.nextRound());
  CHECK(g.publicView().czar == c);
  Fixture small;
  small.deck.answersCount = 14;
  Game another;
  CHECK(another.configure(small.deck, Rules{10, 0, true}, 2));
  another.join("A");
  another.join("B");
  another.join("C");
  CHECK(!another.start());
  CHECK(another.phase() == Phase::Lobby);
}
// Starts a 4-seat game on the pick-1 prompt only (masks keep the others out).
struct Table4 {
  Fixture f;
  uint16_t promptMasks[3] = {0, 1, 1};
  Game g;
  Table4(Rules r = Rules{5, 3, false}) {
    f.deck.promptMasks = promptMasks;
    CHECK(g.configure(f.deck, r, 77));
    g.setEnabled(0);
    g.join("A");
    g.join("B");
    g.join("C");
    g.join("D");
    CHECK(g.start());
  }
  void answerAll() {
    const auto v = g.publicView();
    const uint8_t s[1] = {0};
    for (uint8_t i = 0; i < 4; ++i)
      if (i != v.czar && g.publicView().phase == Phase::Playing) g.submit(i, s, 1);
  }
};
void hostControls() {
  {  // needed(): open answerers while playing, the Juror while judging.
    Table4 t;
    const auto v = t.g.publicView();
    CHECK(v.prompt == 0);
    for (uint8_t i = 0; i < 4; ++i) CHECK(t.g.needed(i) == (i != v.czar));
    const uint8_t s[1] = {0};
    const uint8_t other = static_cast<uint8_t>((v.czar + 1) % 4);
    CHECK(t.g.submit(other, s, 1));
    CHECK(!t.g.needed(other));
    CHECK(t.g.connected(static_cast<uint8_t>((v.czar + 2) % 4), false));
    CHECK(t.g.needed(static_cast<uint8_t>((v.czar + 2) % 4)));  // held turn still blocks
    CHECK(t.g.releaseTurn(static_cast<uint8_t>((v.czar + 2) % 4)));
    CHECK(!t.g.needed(static_cast<uint8_t>((v.czar + 2) % 4)));
    t.answerAll();
    CHECK(t.g.phase() == Phase::Judging);
    for (uint8_t i = 0; i < 4; ++i) CHECK(t.g.needed(i) == (i == v.czar));
  }
  {  // Kicking an answerer who already played removes the private play.
    Table4 t;
    const auto v = t.g.publicView();
    const uint8_t s[1] = {0};
    const uint8_t x = static_cast<uint8_t>((v.czar + 1) % 4);
    CHECK(t.g.submit(x, s, 1));
    CHECK(t.g.publicView().submitted == 1);
    CHECK(t.g.kick(x));
    CHECK(t.g.publicView().submitted == 0);
    CHECK(!t.g.publicView().players[x].seated);
    CHECK(t.g.phase() == Phase::Playing);
    t.answerAll();
    CHECK(t.g.phase() == Phase::Judging);
    CHECK(t.g.publicView().plays == 2);
  }
  {  // Kicking the last open answerer moves the round on.
    Table4 t;
    const auto v = t.g.publicView();
    const uint8_t s[1] = {0};
    CHECK(t.g.submit(static_cast<uint8_t>((v.czar + 1) % 4), s, 1));
    CHECK(t.g.submit(static_cast<uint8_t>((v.czar + 2) % 4), s, 1));
    CHECK(t.g.kick(static_cast<uint8_t>((v.czar + 3) % 4)));
    CHECK(t.g.phase() == Phase::Judging);
  }
  {  // A kicked Juror ends the round without points; the next round has a new Juror.
    Table4 t;
    const auto v = t.g.publicView();
    t.answerAll();
    CHECK(t.g.phase() == Phase::Judging);
    CHECK(t.g.kick(v.czar));
    CHECK(t.g.phase() == Phase::Reveal);
    const auto r = t.g.publicView();
    CHECK(r.aborted && r.winner == kNone);
    for (const auto& p : r.players) CHECK(p.points == 0);
    CHECK(t.g.nextRound());
    CHECK(t.g.publicView().czar != v.czar);
    CHECK(t.g.publicView().players[t.g.publicView().czar].seated);
  }
  {  // A shown answer whose author was kicked can win, but scores nothing.
    Table4 t;
    const auto v = t.g.publicView();
    t.answerAll();
    uint8_t victim = kNone;
    for (uint8_t i = 0; i < 4; ++i)
      if (i != v.czar) {
        victim = i;
        break;
      }
    CHECK(t.g.kick(victim));
    CHECK(t.g.phase() == Phase::Judging);
    // Find the orphaned play by trying each; the orphan gives nobody a point.
    const auto j = t.g.publicView();
    uint8_t pickIndex = kNone;
    for (uint8_t i = 0; i < j.plays; ++i) {
      Game copy = t.g;
      if (copy.judge(v.czar, i) && copy.publicView().winner == kNone) pickIndex = i;
    }
    CHECK(pickIndex != kNone);
    CHECK(t.g.judge(v.czar, pickIndex));
    for (const auto& p : t.g.publicView().players) CHECK(p.points == 0);
  }
  {  // Fewer than three seats left ends the game; a cut round shows no pile.
    Table4 t;
    const auto v = t.g.publicView();
    const uint8_t s[1] = {0};
    CHECK(t.g.submit(static_cast<uint8_t>((v.czar + 1) % 4), s, 1));
    CHECK(t.g.kick(static_cast<uint8_t>((v.czar + 2) % 4)));
    CHECK(t.g.phase() == Phase::Playing);
    CHECK(t.g.kick(static_cast<uint8_t>((v.czar + 3) % 4)));
    CHECK(t.g.phase() == Phase::Finished);
    CHECK(t.g.publicView().plays == 0);
    CHECK(!t.g.kick(static_cast<uint8_t>((v.czar + 3) % 4)));  // seat already empty
  }
  {  // Host end: from any running phase, never from the lobby or twice.
    Table4 t;
    CHECK(t.g.endGame());
    CHECK(t.g.phase() == Phase::Finished);
    CHECK(!t.g.endGame());
    CHECK(t.g.publicView().plays == 0);
    CHECK(t.g.rematch());
    CHECK(!t.g.endGame());
    Table4 u;
    u.answerAll();
    CHECK(u.g.endGame());
    CHECK(u.g.publicView().plays == 0);
  }
  {  // Rando from the next round on.
    Table4 t(Rules{5, 5, false});
    CHECK(t.g.publicView().plays == 0);
    t.g.setRando(true);
    CHECK(t.g.rules().rando);
    t.answerAll();
    CHECK(t.g.judge(t.g.publicView().czar, 0));
    CHECK(t.g.nextRound());
    t.answerAll();
    CHECK(t.g.publicView().plays == 4);  // three players plus Rando
    t.g.setRando(false);
    CHECK(t.g.judge(t.g.publicView().czar, 0));
    CHECK(t.g.nextRound());
    t.answerAll();
    CHECK(t.g.publicView().plays == 3);
  }
  {  // Pack switch between rounds: fresh hands, points kept, next round starts.
    Table4 t;
    t.answerAll();
    const uint8_t juror = t.g.publicView().czar;
    CHECK(t.g.judge(juror, 0));
    uint16_t points[4] = {};
    for (uint8_t i = 0; i < 4; ++i) points[i] = t.g.publicView().players[i].points;
    CHECK(t.g.publicView().round == 1);
    Fixture other;
    for (int i = 0; i < 160; ++i) std::snprintf(other.txt[i], 24, "Other answer %d", i);
    t.f.deck.answers = other.white;  // the Deck object the game points at
    CHECK(t.g.reloadDeck());
    CHECK(t.g.phase() == Phase::Playing);
    CHECK(t.g.publicView().round == 2);
    for (uint8_t i = 0; i < 4; ++i) {
      CHECK(t.g.publicView().players[i].points == points[i]);
      HandView h{};
      CHECK(t.g.privateHand(i, h));
      CHECK(h.size == 5);
    }
    CHECK(!t.g.reloadDeck());  // only between rounds
    Table4 lobby;
    CHECK(lobby.g.endGame());
    CHECK(lobby.g.rematch());
    CHECK(lobby.g.reloadDeck());
    CHECK(lobby.g.phase() == Phase::Lobby);
  }
  {  // Masks: nothing playable means no start; no prompt repeats while there is a choice.
    Fixture f;
    uint16_t pm[3] = {1, 1, 1};
    f.deck.promptMasks = pm;
    Game g;
    CHECK(g.configure(f.deck, Rules{5, 0, false}, 3));
    g.setEnabled(0);
    g.join("A");
    g.join("B");
    g.join("C");
    CHECK(!g.start());
    CHECK(g.promptsOn() == 0);
    g.setEnabled(1);
    CHECK(g.promptsOn() == 3);
    uint16_t am[160] = {};
    for (int i = 0; i < 160; ++i) am[i] = i < 20 ? 0 : 2;
    f.deck.answerMasks = am;
    CHECK(g.answersOn() == 20);
    CHECK(g.answersNeeded(3) == 3 * 5 + 2 * 2);
    CHECK(g.start());
  }
}
int main() {
  validation();
  secretAndRound();
  pickAndRando();
  disconnectAndBounds();
  hostControls();
  std::printf("cardparty: %d assertions, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
