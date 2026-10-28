// Host match control: clocks, extension, auto mode, pause rights and the
// 15 s wait for a needed player. Time is passed in, so every edge is exact.
#include <cstdio>

#include "CardPartyTable.h"
using namespace cardparty;
static int checks = 0, failures = 0;
#define CHECK(x)                                                     \
  do {                                                               \
    ++checks;                                                        \
    if (!(x)) {                                                      \
      ++failures;                                                    \
      std::printf("FAILED %s:%d %s\n", __FILE__, __LINE__, #x);      \
    }                                                                \
  } while (0)

struct Fixture {
  char txt[160][24] = {};
  Answer white[160] = {};
  Prompt black[1] = {{"Server outage: ____.", 1, 0}};
  Deck deck{};
  Game game;
  Table table{game};
  explicit Fixture(Settings s, uint8_t seats = 4) {
    for (int i = 0; i < 160; ++i) {
      std::snprintf(txt[i], 24, "Answer %d", i);
      white[i].text = txt[i];
    }
    deck = Deck{black, 1, white, 160};
    CHECK(game.configure(deck, Rules{5, s.rounds, s.rando}, 41));
    for (uint8_t i = 0; i < seats; ++i) {
      char name[4] = {'P', static_cast<char>('0' + i), 0, 0};
      game.join(name);
    }
    table.reset();
    table.apply(s, 0);
    CHECK(game.start());
    table.tick(1000);
  }
  uint8_t juror() const { return game.publicView().czar; }
  uint8_t answerer(uint8_t k) const { return static_cast<uint8_t>((juror() + 1 + k) % 4); }
  bool answer(uint8_t seat) {
    const uint8_t slot[1] = {0};
    return game.submit(seat, slot, 1);
  }
};

void answerClockAndExtension() {
  Settings s;
  s.answerSecs = 30;
  s.extensionSecs = 10;
  Fixture f(s);
  CHECK(f.table.clock() == Table::Clock::Answers);
  CHECK(f.table.left(1000) == 30000 && f.table.left(21000) == 10000);
  CHECK(f.answer(f.answerer(0)));
  CHECK(!f.table.tick(30999));
  // Runs out with answers missing: one extension, visible to everyone.
  CHECK(f.table.tick(31000));
  CHECK(f.table.extended() && f.table.left(31000) == 10000 && f.game.phase() == Phase::Playing);
  // The second run-out closes submissions with what is there.
  CHECK(f.table.tick(41000));
  CHECK(f.game.phase() == Phase::Judging && f.game.publicView().submitted == 1);
  CHECK(f.table.clock() == Table::Clock::None);  // no Juror clock set
}

void noExtensionAndNobodyAnswered() {
  Settings s;
  s.answerSecs = 30;
  s.extensionSecs = 0;
  Fixture f(s);
  CHECK(f.table.tick(31000));
  // Nobody answered: the round ends without points.
  CHECK(f.game.phase() == Phase::Reveal && f.game.publicView().aborted);
}

void hostExtraTime() {
  Settings s;
  s.answerSecs = 30;
  s.extensionSecs = 0;
  Fixture f(s);
  CHECK(f.table.addTime(11000));  // no extension set: 15 s
  CHECK(f.table.left(11000) == 35000);
  CHECK(f.table.addTime(11000));
  CHECK(f.table.left(11000) == 50000);
  Settings off;
  Fixture g(off);
  CHECK(!g.table.addTime(2000));  // no clock to extend
}

void jurorClockEndsWithoutPoints() {
  Settings s;
  s.jurorSecs = 30;
  Fixture f(s);
  for (uint8_t k = 0; k < 3; ++k) CHECK(f.answer(f.answerer(k)));
  CHECK(f.table.tick(2000));
  CHECK(f.table.clock() == Table::Clock::Juror && f.table.left(2000) == 30000);
  CHECK(f.table.tick(32000));
  const auto v = f.game.publicView();
  CHECK(v.phase == Phase::Reveal && v.aborted && v.winner == kNone);
  for (const auto& p : v.players) CHECK(p.points == 0);
}

void autoModeAndLastRound() {
  Settings s;
  s.autoSecs = 8;
  s.rounds = 2;
  Fixture f(s);
  for (uint8_t k = 0; k < 3; ++k) CHECK(f.answer(f.answerer(k)));
  CHECK(f.game.judge(f.juror(), 0));
  CHECK(f.table.tick(5000));
  CHECK(f.table.clock() == Table::Clock::Next);
  CHECK(!f.table.tick(12999));
  CHECK(f.table.tick(13000));
  CHECK(f.game.phase() == Phase::Playing && f.game.publicView().round == 2);
  for (uint8_t k = 0; k < 3; ++k) CHECK(f.answer(f.answerer(k)));
  CHECK(f.game.judge(f.juror(), 0));
  f.table.tick(14000);
  CHECK(f.table.tick(22000));
  // After the last round: standings stay up, no clock, the host decides.
  CHECK(f.game.phase() == Phase::Finished && f.table.clock() == Table::Clock::None);
  // Switching auto mode on in the reveal arms it right away.
  Settings manual;
  Fixture g(manual);
  for (uint8_t k = 0; k < 3; ++k) CHECK(g.answer(g.answerer(k)));
  CHECK(g.game.judge(g.juror(), 0));
  g.table.tick(2000);
  CHECK(g.table.clock() == Table::Clock::None);
  manual.autoSecs = 5;
  g.table.apply(manual, 3000);
  CHECK(g.table.tick(8000) && g.game.phase() == Phase::Playing);
}

void pauseRightsAndFrozenClock() {
  Settings s;
  s.answerSecs = 60;
  Fixture f(s);
  const uint8_t a = f.answerer(0), b = f.answerer(1);
  CHECK(f.table.pause(a, 11000));
  CHECK(f.table.pausedBy() == a && f.table.frozen());
  CHECK(!f.table.pause(b, 12000));  // one pause at a time
  CHECK(!f.table.unpause(b, 12000));  // not theirs
  CHECK(f.table.left(500000) == 50000);  // standing still
  CHECK(!f.table.tick(500000));
  CHECK(f.table.unpause(a, 500000));
  CHECK(f.table.left(500000) == 50000 && f.table.left(510000) == 40000);
  CHECK(f.table.pause(b, 520000));
  CHECK(f.table.unpause(kHost, 521000));  // the host may always
  CHECK(f.table.pause(kHost, 522000));
  CHECK(!f.table.unpause(a, 523000));  // a host pause is the host's
  CHECK(f.table.unpause(kHost, 523000));
  CHECK(!f.table.pause(9, 524000));  // no such seat
}

void blockAfterFifteenSeconds() {
  Settings s;
  s.answerSecs = 60;
  Fixture f(s);
  const uint8_t a = f.answerer(0), b = f.answerer(1), c = f.answerer(2);
  CHECK(f.answer(a));
  CHECK(f.game.connected(b, false));
  f.table.tick(2000);
  CHECK(f.table.blockedOn() == kNone);
  CHECK(!f.table.tick(16999));
  CHECK(f.table.tick(17000));
  CHECK(f.table.blockedOn() == b && f.table.frozen());
  CHECK(f.table.left(17000) == 44000 && f.table.left(99000) == 44000);
  // Comes back: the block lifts by itself and the clock runs again.
  CHECK(f.game.connected(b, true));
  CHECK(f.table.tick(99000));
  CHECK(f.table.blockedOn() == kNone && f.table.left(100000) == 43000);
  // A player who is not needed never blocks.
  CHECK(f.answer(b));
  CHECK(f.game.connected(a, false));
  CHECK(!f.table.tick(200000) || f.table.blockedOn() == kNone);
  CHECK(f.table.blockedOn() == kNone);
  // The host goes on without a missing answerer.
  CHECK(f.game.connected(c, false));
  f.table.tick(200000);
  f.table.tick(215000);
  CHECK(f.table.blockedOn() == c);
  CHECK(f.table.continueWithout(215000));
  CHECK(f.game.phase() == Phase::Judging && f.table.blockedOn() == kNone);
  // A missing Juror: going on ends the round without points.
  const uint8_t juror = f.juror();
  CHECK(f.game.connected(juror, false));
  f.table.tick(216000);
  f.table.tick(231000);
  CHECK(f.table.blockedOn() == juror);
  CHECK(f.table.continueWithout(231000));
  CHECK(f.game.phase() == Phase::Reveal && f.game.publicView().aborted);
  CHECK(!f.table.continueWithout(231000));
}

void votesAndHostEnd() {
  Settings s;
  Fixture f(s);
  CHECK(!f.table.vote(0));  // only after the match
  CHECK(f.game.endGame());
  f.table.endedByHost();
  f.table.tick(2000);
  CHECK(f.table.ended());
  CHECK(f.table.vote(0) && f.table.vote(2) && f.table.vote(2));
  CHECK(f.table.votes() == 2 && f.table.voted(2) && !f.table.voted(1));
  f.table.seatGone(2);
  CHECK(f.table.votes() == 1);
  CHECK(f.game.rematch());
  f.table.tick(3000);
  CHECK(f.table.votes() == 0 && !f.table.ended());
  CHECK(!f.table.pause(0, 3000));  // nothing to pause in the lobby
}

int main() {
  answerClockAndExtension();
  noExtensionAndNobodyAnswered();
  hostExtraTime();
  jurorClockEndsWithoutPoints();
  autoModeAndLastRound();
  pauseRightsAndFrozenClock();
  blockAfterFifteenSeconds();
  votesAndHostEnd();
  std::printf("cardparty table: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
