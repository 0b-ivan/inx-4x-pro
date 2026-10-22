#include <cstdio>
#include <type_traits>

#include "../../src/apps_local/player/PlayerProgression.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_progression.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

player::GameStats stats(player::GameId game, uint32_t xp) {
  player::GameStats value{};
  value.game = game;
  value.xp = xp;
  return value;
}

void testCompactProfile() {
  static_assert(sizeof(player::StyleProfile) == 6, "radar payload grew unexpectedly");
  static_assert(std::is_trivially_copyable<player::StyleProfile>::value,
                "radar payload must stay trivially copyable");

  player::StyleProfile style{};
  CHECK(style.strategy == 0);
  CHECK(style.versatility == 0);
}

void testGlobalXpAndLevels() {
  const player::GameStats games[] = {
      stats(player::GameId::Battleship, 1200),
      stats(player::GameId::Chess, 3300),
      stats(player::GameId::ConnectFour, 500),
  };

  CHECK(player::ProgressionSystem::totalXp(games, 3) == 5000);
  CHECK(player::ProgressionSystem::xpForLevel(1) == 0);
  CHECK(player::ProgressionSystem::xpForLevel(2) == 100);
  CHECK(player::ProgressionSystem::xpForLevel(3) == 300);
  CHECK(player::ProgressionSystem::xpForLevel(10) == 4500);
  CHECK(player::ProgressionSystem::xpForLevel(25) == 30000);
  CHECK(player::ProgressionSystem::xpForLevel(100) == 495000);

  CHECK(player::ProgressionSystem::levelForXp(0) == 1);
  CHECK(player::ProgressionSystem::levelForXp(99) == 1);
  CHECK(player::ProgressionSystem::levelForXp(100) == 2);
  CHECK(player::ProgressionSystem::levelForXp(299) == 2);
  CHECK(player::ProgressionSystem::levelForXp(300) == 3);
  CHECK(player::ProgressionSystem::levelForXp(5000) == 10);
  CHECK(player::ProgressionSystem::levelForXp(UINT32_MAX) == 100);
}

void testGameStylesAndClasses() {
  const player::GameStats battleship[] = {stats(player::GameId::Battleship, 1000)};
  const player::StyleProfile fleet = player::ProgressionSystem::styleFor(battleship, 1);
  CHECK(fleet.strategy == 82);
  CHECK(fleet.tactics == 68);
  CHECK(fleet.precision == 92);
  CHECK(fleet.risk == 35);
  CHECK(fleet.endurance == 62);
  CHECK(fleet.versatility == 45);
  CHECK(player::ProgressionSystem::classFor(fleet) == player::PlayerClass::Commander);

  const player::GameStats chess[] = {stats(player::GameId::Chess, 1000)};
  CHECK(player::ProgressionSystem::classFor(player::ProgressionSystem::styleFor(chess, 1)) ==
        player::PlayerClass::Strategist);

  const player::GameStats connectFour[] = {stats(player::GameId::ConnectFour, 1000)};
  CHECK(player::ProgressionSystem::classFor(player::ProgressionSystem::styleFor(connectFour, 1)) ==
        player::PlayerClass::Tactician);

  const player::GameStats yahtzee[] = {stats(player::GameId::Yahtzee, 1000)};
  CHECK(player::ProgressionSystem::classFor(player::ProgressionSystem::styleFor(yahtzee, 1)) ==
        player::PlayerClass::FortuneSeeker);
}

void testPlayMixShapesProfile() {
  const player::GameStats mostlyChess[] = {
      stats(player::GameId::Chess, 9000),
      stats(player::GameId::Battleship, 1000),
  };
  const player::StyleProfile chessHeavy = player::ProgressionSystem::styleFor(mostlyChess, 2);
  CHECK(chessHeavy.strategy == 98);
  CHECK(chessHeavy.risk == 22);
  CHECK(player::ProgressionSystem::classFor(chessHeavy) == player::PlayerClass::Strategist);

  const player::GameStats broad[] = {
      stats(player::GameId::Battleship, 1000),
      stats(player::GameId::Chess, 1000),
      stats(player::GameId::Checkers, 1000),
      stats(player::GameId::ConnectFour, 1000),
      stats(player::GameId::Yahtzee, 1000),
      stats(player::GameId::Knucklebones, 1000),
      stats(player::GameId::Jaipur, 1000),
      stats(player::GameId::SeaSalt, 1000),
      stats(player::GameId::ToyBattle, 1000),
  };
  const player::StyleProfile balanced = player::ProgressionSystem::styleFor(broad, 9);
  CHECK(player::ProgressionSystem::classFor(balanced) == player::PlayerClass::AllRounder);
}

void testUnknownAndSnapshot() {
  const player::GameStats games[] = {
      stats(player::GameId::Unknown, 9000),
      stats(player::GameId::Battleship, 1200),
  };

  // Unknown XP still counts globally, but cannot influence a style axis.
  CHECK(player::ProgressionSystem::totalXp(games, 2) == 10200);
  const player::StyleProfile style = player::ProgressionSystem::styleFor(games, 2);
  CHECK(style.strategy == 82);

  const player::ProgressionSnapshot summary = player::ProgressionSystem::summarize(games, 2);
  CHECK(summary.xp == 10200);
  CHECK(summary.level == player::ProgressionSystem::levelForXp(10200));
  CHECK(summary.playerClass == player::PlayerClass::Commander);
  CHECK(summary.style.precision == 92);

  CHECK(player::ProgressionSystem::totalXp(nullptr, 10) == 0);
  CHECK(player::ProgressionSystem::styleFor(nullptr, 10).strategy == 0);
  CHECK(player::ProgressionSystem::classFor(player::StyleProfile{}) == player::PlayerClass::AllRounder);
}

}  // namespace

int main() {
  testCompactProfile();
  testGlobalXpAndLevels();
  testGameStylesAndClasses();
  testPlayMixShapesProfile();
  testUnknownAndSnapshot();

  std::printf("player progression: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
