#pragma once

#include <cstddef>
#include <cstdint>

#include "GuestSession.h"
#include "Match.h"
#include "PlayerAuth.h"
#include "PlayerStore.h"
#include "RandomSource.h"

namespace player {

enum class PlayerServiceResult : uint8_t {
  Ok = 0,
  InvalidArgument,
  RandomUnavailable,
  PlayerNotFound,
  GuestNotEligible,
  NameTaken,
  InvalidPin,
  CryptoError,
  StorageError,
};

class PlayerService {
 public:
  PlayerService(PlayerStore& store, RandomFill randomFill, void* randomContext = nullptr)
      : store_(store),
        randomFill_(randomFill),
        randomContext_(randomContext),
        auth_(store, randomFill, randomContext) {}

  PlayerServiceResult createGuest(GuestSession& out);

  // A guest becomes persistent only after at least one completed match. The
  // guest keeps its generated PlayerId/callsign, and every accumulated game row
  // is inserted atomically with the new profile and PIN credential.
  PlayerServiceResult registerGuest(GuestSession& guest, const char* name, const char* pin,
                                    uint64_t createdAt, Player& out);

  AuthResult authenticate(const PlayerId& playerId, const char* pin) {
    return auth_.authenticate(playerId, pin);
  }

  // Record one authoritative local result. This is the path for games whose
  // opponent is not another profile in this database (computer, browser client
  // or a player represented on another X4). No synthetic opponent PlayerId is
  // created merely to make progression work. Registered profiles are persisted
  // immediately; guest W/L/D, streak, XP and completed-match count stay in the
  // supplied compact GuestSession until that guest is registered.
  PlayerServiceResult recordOutcome(const PlayerId& playerId, GameId game, MatchOutcome outcome,
                                    GuestSession* guest = nullptr);

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
  PlayerAuth auth_;
};

}  // namespace player
