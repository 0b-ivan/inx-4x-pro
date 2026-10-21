#pragma once

#include <cstdint>

namespace leaderboard {

struct Rank {
  uint16_t minimumWins = 0;
  const char* name = "";

  bool ranked() const { return minimumWins != 0 && name != nullptr && name[0] != '\0'; }
};

// Win-based game rank. Rank is derived from persisted wins and intentionally
// never stored, so thresholds/names can be rebalanced without a DB migration.
class RankSystem {
 public:
  static Rank forWins(uint32_t wins);
  static uint32_t nextThreshold(uint32_t wins);
};

}  // namespace leaderboard
