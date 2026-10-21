#pragma once

#include <cstddef>
#include <cstdint>

#include "GameStats.h"

namespace player {

// Global player archetype. It is derived from the style radar and is never
// persisted, so balancing rules can change without a database migration.
enum class PlayerClass : uint8_t {
  Commander = 0,
  Strategist = 1,
  Tactician = 2,
  FortuneSeeker = 3,
  AllRounder = 4,
};

// Compact browser-friendly radar profile. Six normalized 0..100 values are
// enough to render the complete chart; labels and artwork stay client-side.
struct StyleProfile {
  uint8_t strategy = 0;
  uint8_t tactics = 0;
  uint8_t precision = 0;
  uint8_t risk = 0;
  uint8_t endurance = 0;
  uint8_t versatility = 0;
};

static_assert(sizeof(StyleProfile) == 6, "StyleProfile must stay a six-byte payload");

struct ProgressionSnapshot {
  uint32_t xp = 0;
  uint16_t level = 1;
  PlayerClass playerClass = PlayerClass::AllRounder;
  StyleProfile style{};
};

class ProgressionSystem {
 public:
  static constexpr uint16_t kMaxLevel = 100;

  // All methods are allocation-free. GameStats can come from a fixed array,
  // SQLite rows or a small session cache; no map is required.
  static uint32_t totalXp(const GameStats* stats, size_t count);
  static uint32_t xpForLevel(uint16_t level);
  static uint16_t levelForXp(uint32_t xp);
  static StyleProfile styleFor(const GameStats* stats, size_t count);
  static PlayerClass classFor(const StyleProfile& style);
  static ProgressionSnapshot summarize(const GameStats* stats, size_t count);
};

}  // namespace player
