#include "PlayerProgression.h"

#include <climits>

namespace player {
namespace {

struct StyleWeights {
  uint8_t strategy;
  uint8_t tactics;
  uint8_t precision;
  uint8_t risk;
  uint8_t endurance;
  uint8_t versatility;
};

constexpr StyleWeights kGameStyles[] = {
    {0, 0, 0, 0, 0, 0},       // Unknown
    {82, 68, 92, 35, 62, 45},  // Battleship
    {100, 88, 72, 20, 78, 50}, // Chess
    {86, 92, 70, 28, 62, 48},  // Checkers
    {68, 100, 88, 42, 48, 50}, // Connect Four
    {38, 55, 35, 98, 55, 72},  // Yahtzee
    {45, 72, 42, 92, 50, 76},  // Knucklebones
    {65, 82, 58, 78, 62, 86},  // Jaipur
    {52, 68, 48, 94, 58, 88},  // Sea Salt
    {78, 88, 82, 58, 65, 62},  // Toy Battle
};

constexpr size_t kGameStyleCount = sizeof(kGameStyles) / sizeof(kGameStyles[0]);

constexpr StyleWeights kClassProfiles[] = {
    {80, 65, 90, 30, 65, 45},  // Commander
    {100, 85, 70, 20, 75, 50}, // Strategist
    {65, 100, 85, 40, 50, 50}, // Tactician
    {45, 65, 40, 95, 55, 80},  // FortuneSeeker
};

uint8_t average(const uint64_t weighted, const uint64_t xp) {
  if (xp == 0) return 0;
  const uint64_t value = (weighted + (xp / 2U)) / xp;
  return static_cast<uint8_t>(value > 100U ? 100U : value);
}

uint32_t distance(const StyleProfile& style, const StyleWeights& target) {
  const uint8_t actual[] = {style.strategy, style.tactics, style.precision,
                            style.risk, style.endurance, style.versatility};
  const uint8_t wanted[] = {target.strategy, target.tactics, target.precision,
                            target.risk, target.endurance, target.versatility};

  uint32_t total = 0;
  for (size_t i = 0; i < 6; ++i) {
    total += actual[i] > wanted[i] ? actual[i] - wanted[i] : wanted[i] - actual[i];
  }
  return total;
}

bool balanced(const StyleProfile& style) {
  const uint8_t values[] = {style.strategy, style.tactics, style.precision,
                            style.risk, style.endurance, style.versatility};
  uint8_t minimum = values[0];
  uint8_t maximum = values[0];
  for (size_t i = 1; i < 6; ++i) {
    if (values[i] < minimum) minimum = values[i];
    if (values[i] > maximum) maximum = values[i];
  }
  return static_cast<uint8_t>(maximum - minimum) <= 20U;
}

}  // namespace

uint32_t ProgressionSystem::totalXp(const GameStats* stats, const size_t count) {
  if (stats == nullptr || count == 0) return 0;

  uint64_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    total += stats[i].xp;
    if (total >= UINT32_MAX) return UINT32_MAX;
  }
  return static_cast<uint32_t>(total);
}

uint32_t ProgressionSystem::xpForLevel(uint16_t level) {
  if (level <= 1) return 0;
  if (level > kMaxLevel) level = kMaxLevel;

  // 100 XP for level 2, 300 for level 3, 4,500 for level 10,
  // 30,000 for level 25 and 495,000 for level 100.
  const uint64_t previous = static_cast<uint64_t>(level - 1U);
  const uint64_t required = 50U * previous * level;
  return required > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(required);
}

uint16_t ProgressionSystem::levelForXp(const uint32_t xp) {
  uint16_t level = 1;
  while (level < kMaxLevel && xp >= xpForLevel(static_cast<uint16_t>(level + 1U))) {
    ++level;
  }
  return level;
}

StyleProfile ProgressionSystem::styleFor(const GameStats* stats, const size_t count) {
  StyleProfile result{};
  if (stats == nullptr || count == 0) return result;

  uint64_t xp = 0;
  uint64_t strategy = 0;
  uint64_t tactics = 0;
  uint64_t precision = 0;
  uint64_t risk = 0;
  uint64_t endurance = 0;
  uint64_t versatility = 0;

  for (size_t i = 0; i < count; ++i) {
    const size_t game = static_cast<size_t>(stats[i].game);
    if (stats[i].xp == 0 || game == 0 || game >= kGameStyleCount) continue;

    const StyleWeights& weights = kGameStyles[game];
    const uint64_t gameXp = stats[i].xp;
    xp += gameXp;
    strategy += gameXp * weights.strategy;
    tactics += gameXp * weights.tactics;
    precision += gameXp * weights.precision;
    risk += gameXp * weights.risk;
    endurance += gameXp * weights.endurance;
    versatility += gameXp * weights.versatility;
  }

  result.strategy = average(strategy, xp);
  result.tactics = average(tactics, xp);
  result.precision = average(precision, xp);
  result.risk = average(risk, xp);
  result.endurance = average(endurance, xp);
  result.versatility = average(versatility, xp);
  return result;
}

PlayerClass ProgressionSystem::classFor(const StyleProfile& style) {
  if (balanced(style)) return PlayerClass::AllRounder;

  size_t best = 0;
  uint32_t bestDistance = distance(style, kClassProfiles[0]);
  for (size_t i = 1; i < sizeof(kClassProfiles) / sizeof(kClassProfiles[0]); ++i) {
    const uint32_t candidate = distance(style, kClassProfiles[i]);
    if (candidate < bestDistance) {
      best = i;
      bestDistance = candidate;
    }
  }
  return static_cast<PlayerClass>(best);
}

ProgressionSnapshot ProgressionSystem::summarize(const GameStats* stats, const size_t count) {
  ProgressionSnapshot result{};
  result.xp = totalXp(stats, count);
  result.level = levelForXp(result.xp);
  result.style = styleFor(stats, count);
  result.playerClass = classFor(result.style);
  return result;
}

}  // namespace player
