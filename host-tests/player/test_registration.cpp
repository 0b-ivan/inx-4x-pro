#include <cstdio>
#include <string>

#include <unistd.h>

#include "../../src/apps_local/player/PlayerService.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_registration.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

struct RandomState {
  uint8_t next = 1;
};

bool deterministicRandom(void* context, uint8_t* out, const size_t size) {
  auto* state = static_cast<RandomState*>(context);
  if (state == nullptr || out == nullptr) return false;
  for (size_t i = 0; i < size; ++i) out[i] = state->next++;
  return true;
}

std::string tempDatabase() {
  return std::string("/tmp/inx-player-registration-") + std::to_string(static_cast<long long>(getpid())) + ".db";
}

void removeDatabase(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + "-journal").c_str());
}

}  // namespace

int main() {
  const std::string path = tempDatabase();
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  RandomState random{};
  player::PlayerService service(store, deterministicRandom, &random);

  player::GuestSession first{};
  player::GuestSession opponent{};
  CHECK(service.createGuest(first) == player::PlayerServiceResult::Ok);
  CHECK(service.createGuest(opponent) == player::PlayerServiceResult::Ok);

  player::Player profile{};
  CHECK(service.registerGuest(first, "IVAN", "1234", 1789069000ULL, profile) ==
        player::PlayerServiceResult::GuestNotEligible);
  CHECK(service.registerGuest(first, "IVAN", "12x4", 1789069000ULL, profile) ==
        player::PlayerServiceResult::GuestNotEligible);

  const player::PlayerId firstId = first.id;
  const player::Callsign firstCallsign = first.callsign;
  player::GuestSession guests[2] = {first, opponent};
  player::MatchFinishedEvent battle{};
  battle.game = player::GameId::Battleship;
  battle.player1 = guests[0].id;
  battle.player2 = guests[1].id;
  battle.result = player::MatchResult::Player1Win;
  CHECK(service.matchFinished(battle, guests, 2) == player::PlayerServiceResult::Ok);
  first = guests[0];
  opponent = guests[1];

  CHECK(service.registerGuest(first, "IVAN", "12x4", 1789069000ULL, profile) ==
        player::PlayerServiceResult::InvalidPin);
  CHECK(first.active());
  CHECK(first.completedMatches == 1);
  CHECK(service.registerGuest(first, "IVAN", "1234", 1789069000ULL, profile) == player::PlayerServiceResult::Ok);
  CHECK(!first.active());
  CHECK(profile.id == firstId);
  CHECK(profile.callsign.word[player::SlotHair] == firstCallsign.word[player::SlotHair]);
  CHECK(profile.callsign.word[player::SlotEyes] == firstCallsign.word[player::SlotEyes]);
  CHECK(profile.callsign.word[player::SlotMouth] == firstCallsign.word[player::SlotMouth]);
  CHECK(std::string(profile.name) == "IVAN");
  CHECK(profile.createdAt == 1789069000ULL);
  CHECK(service.authenticate(profile.id, "1234") == player::AuthResult::Success);

  player::GameStats stats{};
  CHECK(store.getGameStats(profile.id, player::GameId::Battleship, stats) == player::StoreResult::Ok);
  CHECK(stats.wins == 1);
  CHECK(stats.losses == 0);
  CHECK(stats.draws == 0);
  CHECK(stats.xp == 50);

  // A second guest loses to the registered player and keeps that first loss on registration.
  player::GuestSession loser{};
  CHECK(service.createGuest(loser) == player::PlayerServiceResult::Ok);
  player::GuestSession oneGuest[1] = {loser};
  battle.player1 = loser.id;
  battle.player2 = profile.id;
  battle.result = player::MatchResult::Player2Win;
  CHECK(service.matchFinished(battle, oneGuest, 1) == player::PlayerServiceResult::Ok);
  loser = oneGuest[0];
  CHECK(loser.statsFor(player::GameId::Battleship)->losses == 1);
  CHECK(loser.statsFor(player::GameId::Battleship)->xp == 15);

  player::Player luca{};
  CHECK(service.registerGuest(loser, "LUCA", "4321", 1789069100ULL, luca) == player::PlayerServiceResult::Ok);
  CHECK(store.getGameStats(luca.id, player::GameId::Battleship, stats) == player::StoreResult::Ok);
  CHECK(stats.wins == 0);
  CHECK(stats.losses == 1);
  CHECK(stats.xp == 15);

  // Duplicate names are device-wide and case-insensitive; a failed registration must not consume the guest.
  player::GuestSession duplicate{};
  player::GuestSession duplicateOpponent{};
  CHECK(service.createGuest(duplicate) == player::PlayerServiceResult::Ok);
  CHECK(service.createGuest(duplicateOpponent) == player::PlayerServiceResult::Ok);
  player::GuestSession duplicateGuests[2] = {duplicate, duplicateOpponent};
  battle.player1 = duplicateGuests[0].id;
  battle.player2 = duplicateGuests[1].id;
  battle.result = player::MatchResult::Draw;
  CHECK(service.matchFinished(battle, duplicateGuests, 2) == player::PlayerServiceResult::Ok);
  duplicate = duplicateGuests[0];
  const player::PlayerId duplicateId = duplicate.id;
  CHECK(service.registerGuest(duplicate, "ivan", "9999", 1789069200ULL, profile) ==
        player::PlayerServiceResult::NameTaken);
  CHECK(duplicate.active());
  CHECK(duplicate.id == duplicateId);
  CHECK(duplicate.completedMatches == 1);
  CHECK(duplicate.statsFor(player::GameId::Battleship)->draws == 1);
  CHECK(duplicate.statsFor(player::GameId::Battleship)->xp == 25);

  store.close();
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  RandomState rebootRandom{};
  player::PlayerService afterReboot(store, deterministicRandom, &rebootRandom);
  CHECK(afterReboot.authenticate(profile.id, "1234") == player::AuthResult::Success);
  CHECK(afterReboot.authenticate(luca.id, "4321") == player::AuthResult::Success);
  CHECK(store.getGameStats(profile.id, player::GameId::Battleship, stats) == player::StoreResult::Ok);
  CHECK(stats.wins == 2);
  CHECK(stats.losses == 0);
  CHECK(stats.xp == 100);

  store.close();
  removeDatabase(path);
  std::printf("player registration: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
