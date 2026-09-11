#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#include "../../src/apps_local/player/PlayerStore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_store.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

player::PlayerId idFrom(uint8_t first) {
  player::PlayerId id{};
  for (size_t index = 0; index < id.bytes.size(); ++index) {
    id.bytes[index] = static_cast<uint8_t>(first + index);
  }
  return id;
}

player::Player makePlayer(uint8_t idSeed, const char* name, uint8_t hair, uint8_t eyes, uint8_t mouth) {
  player::Player value{};
  value.id = idFrom(idSeed);
  std::snprintf(value.name, sizeof(value.name), "%s", name);
  value.callsign.word[player::SlotHair] = hair;
  value.callsign.word[player::SlotEyes] = eyes;
  value.callsign.word[player::SlotMouth] = mouth;
  value.createdAt = 1789060000ULL + idSeed;
  return value;
}

std::string tempDatabase(const char* suffix) {
  return std::string("/tmp/inx-player-store-") + std::to_string(static_cast<long long>(getpid())) + "-" + suffix + ".dat";
}

void removeDatabase(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + ".tmp").c_str());
  std::remove((path + ".bak").c_str());
}

void testSchemaAndPlayers() {
  const std::string path = tempDatabase("players");
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  CHECK(store.isOpen());
  CHECK(store.schemaVersion() == player::PlayerStore::kSchemaVersion);

  const player::Player ivan = makePlayer(1, "IVAN", 1, 2, 3);
  const player::Player luca = makePlayer(40, "LUCA", 4, 5, 6);
  CHECK(store.createPlayer(ivan) == player::StoreResult::Ok);
  CHECK(store.createPlayer(luca) == player::StoreResult::Ok);

  player::Player loaded{};
  CHECK(store.getPlayer(ivan.id, loaded) == player::StoreResult::Ok);
  CHECK(loaded.id == ivan.id);
  CHECK(std::strcmp(loaded.name, "IVAN") == 0);
  CHECK(loaded.callsign.word[player::SlotHair] == 1);
  CHECK(loaded.callsign.word[player::SlotEyes] == 2);
  CHECK(loaded.callsign.word[player::SlotMouth] == 3);
  CHECK(loaded.createdAt == ivan.createdAt);

  player::Player byName{};
  CHECK(store.findPlayerByName("ivan", byName) == player::StoreResult::Ok);
  CHECK(byName.id == ivan.id);

  player::Player duplicateName = makePlayer(80, "ivan", 7, 8, 9);
  CHECK(store.createPlayer(duplicateName) == player::StoreResult::NameTaken);

  player::Player invalid{};
  std::snprintf(invalid.name, sizeof(invalid.name), "%s", "NOBODY");
  invalid.callsign.word[player::SlotHair] = 1;
  invalid.callsign.word[player::SlotEyes] = 1;
  invalid.callsign.word[player::SlotMouth] = 1;
  CHECK(store.createPlayer(invalid) == player::StoreResult::InvalidArgument);

  store.close();
  CHECK(!store.isOpen());
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  CHECK(store.getPlayer(luca.id, loaded) == player::StoreResult::Ok);
  CHECK(std::strcmp(loaded.name, "LUCA") == 0);

  store.close();
  removeDatabase(path);
}

void testGameStats() {
  const std::string path = tempDatabase("stats");
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  const player::Player ivan = makePlayer(5, "IVAN", 1, 2, 3);
  CHECK(store.createPlayer(ivan) == player::StoreResult::Ok);

  player::GameStats battleship{};
  battleship.playerId = ivan.id;
  battleship.game = player::GameId::Battleship;
  battleship.wins = 43;
  battleship.losses = 18;
  battleship.currentStreak = 3;
  battleship.bestStreak = 8;
  battleship.xp = 1200;
  CHECK(store.saveGameStats(battleship) == player::StoreResult::Ok);

  player::GameStats chess{};
  chess.playerId = ivan.id;
  chess.game = player::GameId::Chess;
  chess.wins = 21;
  chess.losses = 13;
  chess.draws = 4;
  CHECK(store.saveGameStats(chess) == player::StoreResult::Ok);

  player::GameStats loaded{};
  CHECK(store.getGameStats(ivan.id, player::GameId::Battleship, loaded) == player::StoreResult::Ok);
  CHECK(loaded.wins == 43);
  CHECK(loaded.losses == 18);
  CHECK(loaded.draws == 0);
  CHECK(loaded.currentStreak == 3);
  CHECK(loaded.bestStreak == 8);
  CHECK(loaded.xp == 1200);

  CHECK(store.getGameStats(ivan.id, player::GameId::Chess, loaded) == player::StoreResult::Ok);
  CHECK(loaded.wins == 21);
  CHECK(loaded.losses == 13);
  CHECK(loaded.draws == 4);

  battleship.wins = 44;
  battleship.currentStreak = 4;
  CHECK(store.saveGameStats(battleship) == player::StoreResult::Ok);
  CHECK(store.getGameStats(ivan.id, player::GameId::Battleship, loaded) == player::StoreResult::Ok);
  CHECK(loaded.wins == 44);
  CHECK(loaded.currentStreak == 4);

  player::GameStats missing{};
  missing.playerId = idFrom(100);
  missing.game = player::GameId::Battleship;
  CHECK(store.saveGameStats(missing) == player::StoreResult::NotFound);
  CHECK(store.getGameStats(ivan.id, player::GameId::ConnectFour, loaded) == player::StoreResult::NotFound);

  store.close();
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  CHECK(store.getGameStats(ivan.id, player::GameId::Battleship, loaded) == player::StoreResult::Ok);
  CHECK(loaded.wins == 44);

  store.close();
  removeDatabase(path);
}

void testFutureSchemaIsRejected() {
  const std::string path = tempDatabase("future");
  removeDatabase(path);

  // Minimal valid binary-store header with a future version and empty payload.
  const std::array<unsigned char, 16> header = {
      'X','4','P','L',
      2,0,  // version
      0,0,  // count
      0,0,0,0,  // payload size
      0xc5,0x9d,0x1c,0x81  // FNV-1a checksum of empty payload (2166136261)
  };
  FILE* file = std::fopen(path.c_str(), "wb");
  CHECK(file != nullptr);
  if (file != nullptr) {
    CHECK(std::fwrite(header.data(), 1, header.size(), file) == header.size());
    std::fclose(file);
  }

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::UnsupportedSchema);
  CHECK(!store.isOpen());

  removeDatabase(path);
}

void testCorruptChecksumIsRejected() {
  const std::string path = tempDatabase("corrupt");
  removeDatabase(path);

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  CHECK(store.createPlayer(makePlayer(9, "ALPHA", 1, 1, 1)) == player::StoreResult::Ok);
  store.close();

  FILE* file = std::fopen(path.c_str(), "r+b");
  CHECK(file != nullptr);
  if (file != nullptr) {
    CHECK(std::fseek(file, 12, SEEK_SET) == 0);
    const unsigned char bad = 0;
    CHECK(std::fwrite(&bad, 1, 1, file) == 1);
    std::fclose(file);
  }

  CHECK(store.open(path.c_str()) == player::StoreResult::SqlError);
  CHECK(!store.isOpen());
  removeDatabase(path);
}

}  // namespace

int main() {
  testSchemaAndPlayers();
  testGameStats();
  testFutureSchemaIsRejected();
  testCorruptChecksumIsRejected();

  std::printf("player store: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
