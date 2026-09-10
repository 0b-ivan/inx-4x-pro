#pragma once

#include <cstdint>

namespace player {

// Stable identifiers for games that participate in the shared player system.
// These values will be persisted later, so existing numeric values must not be
// reordered once PlayerStore starts writing them to SQLite.
enum class GameId : uint8_t {
  Unknown = 0,
  Battleship = 1,
  Chess = 2,
  Checkers = 3,
  ConnectFour = 4,
  Yahtzee = 5,
  Knucklebones = 6,
  Jaipur = 7,
  SeaSalt = 8,
  ToyBattle = 9,
};

}  // namespace player
