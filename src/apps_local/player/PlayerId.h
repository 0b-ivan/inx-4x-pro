#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace player {

// Persistent player identity. 128 bits keeps IDs independent from names and
// avoids coordination when profiles are created locally on different devices.
struct PlayerId {
  static constexpr size_t kSize = 16;
  std::array<uint8_t, kSize> bytes{};

  bool empty() const {
    for (const uint8_t byte : bytes) {
      if (byte != 0) return false;
    }
    return true;
  }
};

inline bool operator==(const PlayerId& lhs, const PlayerId& rhs) { return lhs.bytes == rhs.bytes; }
inline bool operator!=(const PlayerId& lhs, const PlayerId& rhs) { return !(lhs == rhs); }

}  // namespace player
