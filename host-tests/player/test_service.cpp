#include <cstdio>
#include <cstring>
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
  std::printf("FAIL test_service.cpp:%d  %s\n", line, what);
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

bool zeroRandom(void*, uint8_t* out, const size_t size) {
  if (out == nullptr) return false;
  std::memset(out, 0, size);
  return true;
}

player::PlayerId idFrom(const uint8_t first) {
  player::PlayerId id{};
  for (size_t index = 0; index < id.bytes.size(); ++index) {
    id.bytes[index] = static_cast<uint8_t>(first + index);
  }
  return id;
}

player::Player makePlayer(const uint8_t idSeed, const char* name) {
  player::Player value{};
  value.id = idFrom(idSeed);
  std::snprintf(value.name, sizeof(value.name), "%s", name);
  value.callsign.word[player::SlotHair] = 1;
  value.callsign.word[player::SlotEyes] = 2;
  value.callsign.word[player::SlotMouth] = 3;
  value.createdAt = 1789060000ULL + idSeed;
  return value;
}

std::string tempDatabase(const char* suffix) {
  return std::string("/tmp/inx-player-service-") + std::to_string(static_cast<long long>(getpid())) + "-" + suffix + ".db";
}

void removeDatabase(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + "-journal").c_str());
}

void testGuestLifecycle() {
  player::PlayerStore store;
  RandomState random{};
  player::PlayerService service(store, deterministicRandom, &random);

  player::GuestSession guests[2]{};
  CHECK(service.createGuest(guests[0]) == player::PlayerServiceResult::Ok);
  CHECK(service.createGuest(guests[1]) == player::PlayerServiceResult::Ok);
  CHECK(guests[0].active());
  CHECK(guests[1].active());
  CHECK(guests[0].id != guests[1].id);
  CHECK(guests[0].completedMatches == 0);

  player::MatchFinishedEvent event{};
  event.game = player::GameId::Battleship;
  event.player1 = guests[0].id;
  event.player2 = guests[1].id;
  event.result = player::MatchResult::Player1Win;
  CHECK(service.matchFinished(event, guests, 2) == player::PlayerServiceResult::Ok);

  const player::GuestGameStats* first = guests[0].statsFor(player::GameId::Battleship);
  const player::GuestGameStats* second = guests[1].statsFor(player::GameId::Battleship);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->wins == 1);
  CHECK(first->losses == 0);
  CHECK(first->xp == 50);
  CHECK(first->currentStreak == 1);
  CHECK(first->bestStreak == 1);
  CHECK(second->losses == 1);
  CHECK(second->xp == 15);

  event.result = player::MatchResult::Player2Win;
  CHECK(service.matchFinished(event, guests, 2) == player::PlayerServiceResult::Ok);
  event.result = player::MatchResult::Draw;
  CHECK(service.matchFinished(event, guests, 2) == player::PlayerServiceResult::Ok);

  first = guests[0].statsFor(player::GameId::Battleship);
  second = guests[1].statsFor(player::GameId::Battleship);
  CHECK(first->wins == 1);
  CHECK(first->losses == 1);
  CHECK(first->draws == 1);
  CHECK(first->xp == 90);
  CHECK(first->currentStreak == 0);
  CHECK(first->bestStreak == 1);
  CHECK(second->wins == 1);
  CHECK(second->losses == 1);
  CHECK(second->draws == 1);
  CHECK(second->xp == 90);
  CHECK(guests[0].completedMatches == 3);
  CHECK(guests[1].completedMatches == 3);
}

void testPersistentAndMixedMatches() {
  const std::string path = tempDatabase("persistent");
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  const player::Player ivan = makePlayer(10, "IVAN");
  const player::Player luca = makePlayer(50, "LUCA");
  CHECK(store.createPlayer(ivan) == player::StoreResult::Ok);
  CHECK(store.createPlayer(luca) == player::StoreResult::Ok);

  RandomState random{};
  player::PlayerService service(store, deterministicRandom, &random);

  player::MatchFinishedEvent chess{};
  chess.game = player::GameId::Chess;
  chess.player1 = ivan.id;
  chess.player2 = luca.id;
  chess.result = player::MatchResult::Player1Win;
  CHECK(service.matchFinished(chess, nullptr, 0) == player::PlayerServiceResult::Ok);
  chess.result = player::MatchResult::Draw;
  CHECK(service.matchFinished(chess, nullptr, 0) == player::PlayerServiceResult::Ok);

  player::GameStats stats{};
  CHECK(store.getGameStats(ivan.id, player::GameId::Chess, stats) == player::StoreResult::Ok);
  CHECK(stats.wins == 1);
  CHECK(stats.draws == 1);
  CHECK(stats.xp == 105);
  CHECK(stats.currentStreak == 0);
  CHECK(stats.bestStreak == 1);

  CHECK(store.getGameStats(luca.id, player::GameId::Chess, stats) == player::StoreResult::Ok);
  CHECK(stats.losses == 1);
  CHECK(stats.draws == 1);
  CHECK(stats.xp == 55);

  player::GuestSession guest{};
  CHECK(service.createGuest(guest) == player::PlayerServiceResult::Ok);
  player::MatchFinishedEvent mixed{};
  mixed.game = player::GameId::Battleship;
  mixed.player1 = guest.id;
  mixed.player2 = ivan.id;
  mixed.result = player::MatchResult::Player1Win;
  CHECK(service.matchFinished(mixed, &guest, 1) == player::PlayerServiceResult::Ok);
  CHECK(guest.statsFor(player::GameId::Battleship)->wins == 1);
  CHECK(guest.statsFor(player::GameId::Battleship)->xp == 50);
  CHECK(guest.completedMatches == 1);

  CHECK(store.getGameStats(ivan.id, player::GameId::Battleship, stats) == player::StoreResult::Ok);
  CHECK(stats.losses == 1);
  CHECK(stats.xp == 15);

  store.close();
  removeDatabase(path);
}

void testNoPartialUpdateForUnknownPlayer() {
  const std::string path = tempDatabase("atomic");
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  const player::Player ivan = makePlayer(20, "IVAN");
  CHECK(store.createPlayer(ivan) == player::StoreResult::Ok);

  RandomState random{};
  player::PlayerService service(store, deterministicRandom, &random);

  player::MatchFinishedEvent event{};
  event.game = player::GameId::Chess;
  event.player1 = ivan.id;
  event.player2 = idFrom(200);
  event.result = player::MatchResult::Player1Win;
  CHECK(service.matchFinished(event, nullptr, 0) == player::PlayerServiceResult::PlayerNotFound);

  player::GameStats stats{};
  CHECK(store.getGameStats(ivan.id, player::GameId::Chess, stats) == player::StoreResult::NotFound);

  player::GuestSession guest{};
  CHECK(service.createGuest(guest) == player::PlayerServiceResult::Ok);
  event.player1 = guest.id;
  CHECK(service.matchFinished(event, &guest, 1) == player::PlayerServiceResult::PlayerNotFound);
  CHECK(guest.completedMatches == 0);
  CHECK(guest.statsFor(player::GameId::Chess)->xp == 0);

  store.close();
  removeDatabase(path);
}

void testValidationAndXpPolicy() {
  player::PlayerStore store;
  player::PlayerService noRandom(store, nullptr);
  player::GuestSession guest{};
  CHECK(noRandom.createGuest(guest) == player::PlayerServiceResult::RandomUnavailable);

  player::PlayerService zeros(store, zeroRandom);
  CHECK(zeros.createGuest(guest) == player::PlayerServiceResult::RandomUnavailable);

  CHECK(player::PlayerService::xpForMatch(player::GameId::Battleship, player::MatchOutcome::Win) == 50);
  CHECK(player::PlayerService::xpForMatch(player::GameId::Chess, player::MatchOutcome::Win) == 70);
  CHECK(player::PlayerService::xpForMatch(player::GameId::ConnectFour, player::MatchOutcome::Loss) == 10);
  CHECK(player::PlayerService::xpForMatch(player::GameId::Unknown, player::MatchOutcome::Win) == 0);

  RandomState random{};
  player::PlayerService service(store, deterministicRandom, &random);
  player::GuestSession guests[2]{};
  CHECK(service.createGuest(guests[0]) == player::PlayerServiceResult::Ok);
  CHECK(service.createGuest(guests[1]) == player::PlayerServiceResult::Ok);

  player::MatchFinishedEvent invalid{};
  invalid.game = player::GameId::Unknown;
  invalid.player1 = guests[0].id;
  invalid.player2 = guests[1].id;
  CHECK(service.matchFinished(invalid, guests, 2) == player::PlayerServiceResult::InvalidArgument);

  invalid.game = player::GameId::Battleship;
  invalid.player2 = invalid.player1;
  CHECK(service.matchFinished(invalid, guests, 2) == player::PlayerServiceResult::InvalidArgument);
  CHECK(service.matchFinished(invalid, nullptr, 2) == player::PlayerServiceResult::InvalidArgument);
}

}  // namespace

int main() {
  testGuestLifecycle();
  testPersistentAndMixedMatches();
  testNoPartialUpdateForUnknownPlayer();
  testValidationAndXpPolicy();

  std::printf("player service: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
