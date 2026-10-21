#include "RankSystem.h"

#include <cstddef>

namespace leaderboard {
namespace {

constexpr Rank kRanks[] = {
    {1, "Maat"},
    {5, "Obermaat"},
    {10, "Kadett"},
    {20, "Fähnrich"},
    {40, "Käpten"},
    {75, "Kommodore"},
    {125, "Konteradmiral"},
    {200, "Vizeadmiral"},
    {300, "Admiral"},
    {500, "Großadmiral"},
    {750, "Flottenlegende"},
    {1000, "Herr der sieben Meere"},
};

constexpr size_t kRankCount = sizeof(kRanks) / sizeof(kRanks[0]);

}  // namespace

Rank RankSystem::forWins(const uint32_t wins) {
  Rank result{};
  for (size_t i = 0; i < kRankCount; ++i) {
    if (wins < kRanks[i].minimumWins) break;
    result = kRanks[i];
  }
  return result;
}

uint32_t RankSystem::nextThreshold(const uint32_t wins) {
  for (size_t i = 0; i < kRankCount; ++i) {
    if (wins < kRanks[i].minimumWins) return kRanks[i].minimumWins;
  }
  return 0;
}

}  // namespace leaderboard
