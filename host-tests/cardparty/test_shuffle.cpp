// Card mixing: uniform deals, no prompt repeats within a session, no answer
// back in play before the draw pile is used up.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
#include "CardPartyCore.h"
using namespace cardparty;

namespace {
int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

struct Fixture {
  std::vector<std::vector<char>> texts;
  std::vector<Prompt> prompts;
  std::vector<Answer> answers;
  Deck deck{};
  Fixture(uint16_t promptCount, uint16_t answerCount) {
    texts.reserve(promptCount + answerCount);
    for (uint16_t i = 0; i < promptCount; ++i) {
      texts.emplace_back(32);
      std::snprintf(texts.back().data(), 32, "Prompt %u ____", i);
      prompts.push_back({texts.back().data(), 1, 0});
    }
    for (uint16_t i = 0; i < answerCount; ++i) {
      texts.emplace_back(32);
      std::snprintf(texts.back().data(), 32, "Answer %u", i);
      answers.push_back({texts.back().data()});
    }
    deck = Deck{prompts.data(), promptCount, answers.data(), answerCount};
  }
};

void seat(Game& g, int players) {
  for (int i = 0; i < players; ++i) {
    char n[8];
    std::snprintf(n, sizeof n, "P%d", i);
    g.join(n);
  }
}

bool playRound(Game& g, int players) {
  const auto v = g.publicView();
  const uint8_t slot[3] = {0, 1, 2};
  for (int i = 0; i < players; ++i)
    if (i != v.czar) g.submit(static_cast<uint8_t>(i), slot, v.pick);
  return g.judge(v.czar, 0) && g.nextRound();
}

// Every card is equally likely to be dealt; neighbours in the deck are not
// dealt together more often than chance.
void uniformDeal() {
  Fixture f(5, 300);
  const int seeds = 3000, players = 8, hand = 10;
  std::vector<int> dealt(300, 0);
  long neighbours = 0;
  for (int s = 1; s <= seeds; ++s) {
    Game g;
    CHECK(g.configure(f.deck, Rules{hand, 3, false}, static_cast<uint32_t>(s * 2654435761u)));
    seat(g, players);
    CHECK(g.start());
    for (uint8_t p = 0; p < players; ++p) {
      HandView h{};
      g.privateHand(p, h);
      std::set<uint16_t> cards(h.cards, h.cards + h.size);
      for (uint16_t c : cards) {
        ++dealt[c];
        if (cards.count(static_cast<uint16_t>(c + 1))) ++neighbours;
      }
    }
  }
  const double expected = seeds * players * hand / 300.0;  // 800
  int worst = 0;
  for (int c : dealt) worst = std::max(worst, static_cast<int>(std::fabs(c - expected)));
  CHECK(worst < expected * 0.15);
  // Chance that a hand of 10 from 300 also holds the next card: 9/299.
  const double chance = seeds * players * hand * (hand - 1) / 299.0;
  CHECK(neighbours < chance * 1.3);
  std::printf("uniform deal: worst card off by %d of %.0f, neighbour pairs %ld (chance %.0f)\n",
              worst, expected, neighbours, chance);
}

// Prompts: all distinct until every one was shown, then a new cycle that
// does not start with the last one. Kept across a rematch.
void promptsDoNotRepeat() {
  Fixture f(7, 200);
  Game g;
  CHECK(g.configure(f.deck, Rules{5, 20, false}, 99));
  seat(g, 3);
  CHECK(g.start());
  std::vector<uint16_t> seen;
  for (int r = 0; r < 20; ++r) {
    seen.push_back(g.publicView().prompt);
    if (r < 19) CHECK(playRound(g, 3));
  }
  for (size_t cycle = 0; cycle + 7 <= seen.size(); cycle += 7)
    CHECK(std::set<uint16_t>(seen.begin() + cycle, seen.begin() + cycle + 7).size() == 7);
  for (size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] != seen[i - 1]);
  // Ten rounds from 12 prompts never repeat.
  Fixture big(12, 200);
  for (uint32_t seed = 1; seed < 200; ++seed) {
    Game h;
    CHECK(h.configure(big.deck, Rules{5, 10, true}, seed));
    seat(h, 4);
    CHECK(h.start());
    std::set<uint16_t> shown;
    for (int r = 0; r < 10; ++r) {
      shown.insert(h.publicView().prompt);
      if (r < 9) playRound(h, 4);
    }
    CHECK(shown.size() == 10);
  }
  std::puts("prompts: no repeat until all shown, no immediate repeat across cycles");
}

// An answer that left play (submitted) does not come back until every other
// playable answer has been drawn once.
void answersCycleThroughDeck() {
  Fixture f(3, 120);
  Game g;
  CHECK(g.configure(f.deck, Rules{5, 0, false}, 7));
  seat(g, 3);
  CHECK(g.start());
  std::vector<int> firstSeen(120, -1);
  int draws = 0, repeatsBeforeExhaustion = 0;
  std::set<uint16_t> everSeen;
  auto collect = [&] {
    for (uint8_t p = 0; p < 3; ++p) {
      HandView h{};
      g.privateHand(p, h);
      for (uint8_t i = 0; i < h.size; ++i) everSeen.insert(h.cards[i]);
    }
  };
  collect();
  std::set<uint16_t> played;
  for (int r = 0; r < 60 && everSeen.size() < 120; ++r) {
    const auto v = g.publicView();
    const uint8_t slot[1] = {0};
    for (uint8_t p = 0; p < 3; ++p)
      if (p != v.czar) {
        HandView h{};
        g.privateHand(p, h);
        if (played.count(h.cards[0])) ++repeatsBeforeExhaustion;
        played.insert(h.cards[0]);
        g.submit(p, slot, 1);
      }
    CHECK(g.judge(v.czar, 0) && g.nextRound());
    collect();
    ++draws;
  }
  CHECK(repeatsBeforeExhaustion == 0);
  CHECK(everSeen.size() == 120);
  std::printf("answers: whole deck of 120 seen after %d rounds, no early repeats\n", draws);
}
// Discard: once per round, only before answering, only while playing; the
// thrown card does not come back before the pile is used up.
void swapOncePerRound() {
  Fixture f(4, 200);
  Game g;
  CHECK(g.configure(f.deck, Rules{5, 10, false}, 4242));
  seat(g, 3);
  CHECK(!g.swap(0, 0));  // lobby
  CHECK(g.start());
  const auto v = g.publicView();
  const uint8_t p = static_cast<uint8_t>((v.czar + 1) % 3);
  HandView before{}, after{};
  g.privateHand(p, before);
  CHECK(!g.swap(p, 5));  // no such slot
  CHECK(g.swap(p, 2));
  g.privateHand(p, after);
  CHECK(after.swapped && after.cards[2] != before.cards[2]);
  CHECK(!g.swap(p, 0));  // once per round
  const uint8_t q = static_cast<uint8_t>((v.czar + 2) % 3);
  const uint8_t slot[1] = {0};
  CHECK(g.submit(q, slot, 1));
  CHECK(!g.swap(q, 1));  // already answered
  CHECK(g.submit(p, slot, 1));
  CHECK(g.judge(v.czar, 0) && g.nextRound());
  HandView next{};
  g.privateHand(p, next);
  CHECK(!next.swapped);
  // The discarded card stays out for the rest of the pile.
  bool back = false;
  for (int r = 0; r < 8; ++r) {
    for (uint8_t s = 0; s < 3; ++s) {
      HandView h{};
      g.privateHand(s, h);
      for (uint8_t i = 0; i < h.size; ++i) back = back || h.cards[i] == before.cards[2];
    }
    const auto w = g.publicView();
    for (uint8_t s = 0; s < 3; ++s)
      if (s != w.czar) g.submit(s, slot, w.pick);
    if (!g.judge(w.czar, 0) || !g.nextRound()) break;
  }
  CHECK(!back);
  std::puts("swap: once per round, not after answering, discarded card stays out");
}
}  // namespace

int main() {
  uniformDeal();
  promptsDoNotRepeat();
  answersCycleThroughDeck();
  swapOncePerRound();
  std::printf("cardparty shuffle: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
