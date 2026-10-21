#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "GameId.h"
#include "Player.h"

namespace player {

// Guest progress deliberately omits PlayerId and GameId: both are already known
// from the owning GuestSession and the fixed array slot. That keeps a complete
// nine-game guest profile small enough to hold in RAM without a hash map.
struct GuestGameStats {
  uint16_t wins = 0;
  uint16_t losses = 0;
  uint16_t draws = 0;
  uint16_t currentStreak = 0;
  uint16_t bestStreak = 0;
  uint32_t xp = 0;
};

constexpr size_t kGuestGameCount = static_cast<size_t>(GameId::ToyBattle);

struct GuestSession {
  PlayerId id{};
  Callsign callsign{};
  std::array<GuestGameStats, kGuestGameCount> games{};
  uint16_t completedMatches = 0;

  bool active() const { return !id.empty() && callsign.known(); }

  GuestGameStats* statsFor(GameId game) {
    const size_t value = static_cast<size_t>(game);
    if (value == 0 || value > kGuestGameCount) return nullptr;
    return &games[value - 1U];
  }

  const GuestGameStats* statsFor(GameId game) const {
    const size_t value = static_cast<size_t>(game);
    if (value == 0 || value > kGuestGameCount) return nullptr;
    return &games[value - 1U];
  }
};

static_assert(sizeof(GuestGameStats) <= 16, "GuestGameStats must stay compact");
static_assert(sizeof(GuestSession) <= 192, "GuestSession is intended as a small RAM-only object");

}  // namespace player
