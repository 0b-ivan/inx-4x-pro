#pragma once

#include <cstddef>
#include <cstdint>

namespace bship {
struct Game;
}
namespace bshipweb {
// Explicit allowlist for the read-only foundation. No fleet, board, pointers,
// or raw Game storage can cross this boundary, even after the game ends.
enum class BrowserPhase : uint8_t { Waiting, Placement, Playing, Finished };
struct BrowserSnapshot {
  BrowserPhase phase = BrowserPhase::Waiting;
  bool connected = false;
  bool myTurn = false;
};
BrowserSnapshot browserSnapshot(const bship::Game& game, bool connected);
// Only this DTO is serializable. Returns zero on insufficient capacity.
size_t serializeSnapshot(const BrowserSnapshot& snapshot, char* out, size_t capacity);
bool sameSnapshot(const BrowserSnapshot& a, const BrowserSnapshot& b);
}  // namespace bshipweb
