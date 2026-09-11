#pragma once

#include <cstddef>
#include <cstdint>

namespace player {

// Shared injectable entropy source. Device integration can wrap esp_fill_random
// while host tests provide deterministic bytes without pulling Arduino into the
// player domain.
using RandomFill = bool (*)(void* context, uint8_t* out, size_t size);

}  // namespace player
