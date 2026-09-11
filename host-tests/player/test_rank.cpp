#include <cassert>
#include <cstring>

#include "../../src/apps_local/leaderboard/RankSystem.h"

namespace {

void expectRank(const uint32_t wins, const char* expected) {
  const leaderboard::Rank rank = leaderboard::RankSystem::forWins(wins);
  assert(std::strcmp(rank.name, expected) == 0);
}

}  // namespace

int main() {
  const leaderboard::Rank unranked = leaderboard::RankSystem::forWins(0);
  assert(!unranked.ranked());
  assert(std::strcmp(unranked.name, "") == 0);
  assert(leaderboard::RankSystem::nextThreshold(0) == 1);

  expectRank(1, "Maat");
  expectRank(4, "Maat");
  expectRank(5, "Obermaat");
  expectRank(9, "Obermaat");
  expectRank(10, "Kadett");
  expectRank(20, "Fähnrich");
  expectRank(40, "Käpten");
  expectRank(75, "Kommodore");
  expectRank(125, "Konteradmiral");
  expectRank(200, "Vizeadmiral");
  expectRank(300, "Admiral");
  expectRank(500, "Großadmiral");
  expectRank(750, "Flottenlegende");
  expectRank(999, "Flottenlegende");
  expectRank(1000, "Herr der sieben Meere");
  expectRank(5000, "Herr der sieben Meere");

  assert(leaderboard::RankSystem::nextThreshold(1) == 5);
  assert(leaderboard::RankSystem::nextThreshold(999) == 1000);
  assert(leaderboard::RankSystem::nextThreshold(1000) == 0);
  return 0;
}
