// Shared player-domain tests. These intentionally need no Arduino, renderer or
// storage so the core identity/stat model can stay cheap to validate on a host.

#include <cstdio>
#include <cstring>
#include <type_traits>

#include "../../src/apps_local/player/GameStats.h"
#include "../../src/apps_local/player/Player.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_domain.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

}  // namespace

int main() {
  static_assert(sizeof(player::PlayerId) == 16, "PlayerId must stay 128-bit");
  static_assert(std::is_trivially_copyable<player::PlayerId>::value, "PlayerId must stay cheap to persist");
  static_assert(std::is_trivially_copyable<player::Player>::value, "Player must stay a plain domain value");
  static_assert(std::is_trivially_copyable<player::GameStats>::value, "GameStats must stay a plain domain value");

  player::PlayerId empty;
  CHECK(empty.empty());

  player::PlayerId first;
  first.bytes[0] = 0x42;
  CHECK(!first.empty());

  player::PlayerId same = first;
  CHECK(first == same);
  same.bytes[15] = 0x01;
  CHECK(first != same);

  player::Player profile;
  CHECK(profile.id.empty());
  CHECK(profile.name[0] == '\0');
  CHECK(profile.callsign.word[player::SlotHair] == player::kUnknownWord);
  CHECK(profile.callsign.word[player::SlotEyes] == player::kUnknownWord);
  CHECK(profile.callsign.word[player::SlotMouth] == player::kUnknownWord);
  CHECK(profile.createdAt == 0);

  std::strcpy(profile.name, "IVAN");
  profile.callsign.word[player::SlotHair] = 1;
  profile.callsign.word[player::SlotEyes] = 2;
  profile.callsign.word[player::SlotMouth] = 3;
  CHECK(std::strcmp(profile.name, "IVAN") == 0);
  CHECK(profile.callsign.word[player::SlotHair] == 1);
  CHECK(profile.callsign.word[player::SlotEyes] == 2);
  CHECK(profile.callsign.word[player::SlotMouth] == 3);

  player::GameStats stats;
  CHECK(stats.playerId.empty());
  CHECK(stats.game == player::GameId::Unknown);
  CHECK(stats.wins == 0);
  CHECK(stats.losses == 0);
  CHECK(stats.draws == 0);
  CHECK(stats.currentStreak == 0);
  CHECK(stats.bestStreak == 0);
  CHECK(stats.xp == 0);

  stats.playerId = first;
  stats.game = player::GameId::Battleship;
  stats.wins = 1;
  CHECK(stats.playerId == first);
  CHECK(stats.game == player::GameId::Battleship);
  CHECK(stats.wins == 1);
  CHECK(player::GameId::Battleship != player::GameId::Chess);
  CHECK(player::GameId::Chess != player::GameId::ConnectFour);

  std::printf("player domain: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
