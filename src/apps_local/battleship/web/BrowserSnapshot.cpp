#include "BrowserSnapshot.h"

#include <cstdio>

#include "../BattleshipCore.h"

namespace bshipweb {
BrowserSnapshot browserSnapshot(const bship::Game& game, const bool connected) {
  BrowserSnapshot result;
  result.connected = connected;
  if (!connected) return result;
  constexpr int browserSide = 1;
  if (bship::over(game))
    result.phase = BrowserPhase::Finished;
  else if (bship::bothPlaced(game))
    result.phase = BrowserPhase::Playing;
  else if (!game.side[browserSide].placed)
    result.phase = BrowserPhase::Placement;
  else
    result.phase = BrowserPhase::Waiting;
  result.myTurn = result.phase == BrowserPhase::Playing && game.turn == browserSide;
  return result;
}
bool sameSnapshot(const BrowserSnapshot& a, const BrowserSnapshot& b) {
  return a.phase == b.phase && a.connected == b.connected && a.myTurn == b.myTurn;
}
size_t serializeSnapshot(const BrowserSnapshot& snapshot, char* out, const size_t capacity) {
  if (!out || !capacity) return 0;
  const char* phase = "waiting";
  switch (snapshot.phase) {
    case BrowserPhase::Placement:
      phase = "placement";
      break;
    case BrowserPhase::Playing:
      phase = "playing";
      break;
    case BrowserPhase::Finished:
      phase = "finished";
      break;
    case BrowserPhase::Waiting:
      break;
  }
  const int count = snprintf(out, capacity, "{\"type\":\"state\",\"phase\":\"%s\",\"connected\":%s,\"myTurn\":%s}",
                             phase, snapshot.connected ? "true" : "false", snapshot.myTurn ? "true" : "false");
  if (count < 0 || static_cast<size_t>(count) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(count);
}
}  // namespace bshipweb
