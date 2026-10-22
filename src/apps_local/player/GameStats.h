#pragma once

#include <cstdint>

#include "GameId.h"
#include "PlayerId.h"

namespace player {

// Per-game progression. Rank is intentionally not stored here: it is derived
// from wins so rank thresholds can change without rewriting persistent data.
struct GameStats {
  PlayerId playerId{};
  GameId game = GameId::Unknown;
  uint32_t wins = 0;
  uint32_t losses = 0;
  uint32_t draws = 0;
  uint32_t currentStreak = 0;
  uint32_t bestStreak = 0;
  uint32_t xp = 0;
};

}  // namespace player
