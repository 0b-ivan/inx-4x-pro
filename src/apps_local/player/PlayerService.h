#pragma once

#include <cstddef>
#include <cstdint>

#include "GuestSession.h"
#include "Match.h"
#include "PlayerStore.h"

namespace player {

enum class PlayerServiceResult : uint8_t {
  Ok = 0,
  InvalidArgument,
  RandomUnavailable,
  PlayerNotFound,
  StorageError,
};

// Injectable so the shared service remains host-testable and does not hardwire
// esp_random()/Arduino into the domain layer. Device integration can supply a
// tiny adapter around esp_fill_random().
using RandomFill = bool (*)(void* context, uint8_t* out, size_t size);

class PlayerService {
 public:
  PlayerService(PlayerStore& store, RandomFill randomFill, void* randomContext = nullptr)
      : store_(store), randomFill_(randomFill), randomContext_(randomContext) {}

  PlayerServiceResult createGuest(GuestSession& out);

  // `guests` is the small set of RAM-only guest sessions participating in the
  // current match. Resolution is a linear scan over that tiny set, not a hash
  // map. Any participant not present there is treated as a persistent player.
  PlayerServiceResult matchFinished(const MatchFinishedEvent& event, GuestSession* guests,
                                    size_t guestCount);

  static uint16_t xpForMatch(GameId game, MatchOutcome outcome);

 private:
  PlayerStore& store_;
  RandomFill randomFill_ = nullptr;
  void* randomContext_ = nullptr;
};

}  // namespace player
