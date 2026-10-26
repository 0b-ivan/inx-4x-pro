#pragma once

#include <cstddef>
#include <cstdint>

namespace bship {
struct Game;
}
namespace bshipweb {

enum class BrowserPhase : uint8_t { Waiting, Placement, Playing, Finished };

struct BrowserSnapshot {
  BrowserPhase phase = BrowserPhase::Waiting;
  bool connected = false;
  bool myTurn = false;
  int8_t winner = -1;
  // Public-to-browser information only. No unshot X4 fleet cells are exposed.
  uint8_t shotsAtX4[13] = {};
  uint8_t hitsAtX4[13] = {};
  uint8_t shotsAtBrowser[13] = {};
  // One bit per X4 ship, revealed only after that ship is sunk.
  uint8_t sunkAtX4 = 0;
  // Public damage summary for the opponent fleet. Counts reveal only how many
  // segments of each ship have already been hit, never bow/orientation/cells.
  uint8_t hitsByX4Ship[5] = {};
};

BrowserSnapshot browserSnapshot(const bship::Game& game, bool connected);
size_t serializeSnapshot(const BrowserSnapshot& snapshot, char* out, size_t capacity);
size_t serializeFleetStatus(const BrowserSnapshot& snapshot, char* out, size_t capacity);
bool sameSnapshot(const BrowserSnapshot& a, const BrowserSnapshot& b);

}  // namespace bshipweb
