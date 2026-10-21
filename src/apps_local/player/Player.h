#pragma once

#include <cstddef>
#include <cstdint>

#include "PlayerId.h"
#include "PlayerName.h"

namespace player {

// The visible account name is separate from the generated three-word identity.
// PlayerName::Name remains the generated callsign/avatar source for now; this
// keeps the existing multiplayer/avatar code working while the persistent
// player system is introduced incrementally.
constexpr size_t kMaxPlayerNameLength = 20;
using Callsign = Name;

struct Player {
  PlayerId id{};
  char name[kMaxPlayerNameLength + 1] = {};
  Callsign callsign{};
  uint64_t createdAt = 0;
};

}  // namespace player
