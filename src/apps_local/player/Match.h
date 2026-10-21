#pragma once

#include <cstdint>

#include "GameId.h"
#include "PlayerId.h"

namespace player {

enum class MatchResult : uint8_t {
  Player1Win = 1,
  Player2Win = 2,
  Draw = 3,
};

enum class MatchOutcome : uint8_t {
  Win = 0,
  Loss = 1,
  Draw = 2,
};

// Shared two-player result emitted by a game engine. The event intentionally
// contains no XP, rank or class supplied by the browser. PlayerService derives
// progression from the authoritative game and result.
struct MatchFinishedEvent {
  GameId game = GameId::Unknown;
  PlayerId player1{};
  PlayerId player2{};
  MatchResult result = MatchResult::Draw;
  uint64_t endedAt = 0;
};

}  // namespace player
