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
  std::printf("FAIL test_directory.cpp:%d  %s\n", line, what);
}

#define CHECK(expr) check((expr), #expr, __LINE__)

player::Player makePlayer(const uint8_t idByte, const char* name, const uint64_t createdAt) {
  player::Player value{};
  value.id.bytes.back() = idByte;
  std::snprintf(value.name, sizeof(value.name), "%s", name);
  value.callsign.word[player::SlotHair] =
      static_cast<uint8_t>(idByte % player::kWordCount[player::SlotHair]);
  value.callsign.word[player::SlotEyes] =
      static_cast<uint8_t>((idByte + 1) % player::kWordCount[player::SlotEyes]);
  value.callsign.word[player::SlotMouth] =
      static_cast<uint8_t>((idByte + 2) % player::kWordCount[player::SlotMouth]);
  value.createdAt = createdAt;
  return value;
}

}  // namespace

int main() {
  const std::string path = std::string("/tmp/inx-player-directory-") +
                           std::to_string(static_cast<long long>(getpid())) + ".db";
  std::remove(path.c_str());

  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);
  CHECK(store.createPlayer(makePlayer(1, "zoe", 30)) == player::StoreResult::Ok);
  CHECK(store.createPlayer(makePlayer(2, "IVAN", 20)) == player::StoreResult::Ok);
  CHECK(store.createPlayer(makePlayer(3, "anna", 10)) == player::StoreResult::Ok);

  player::Player players[8]{};
  size_t count = 99;
  CHECK(store.listPlayers(players, 8, count) == player::StoreResult::Ok);
  CHECK(count == 3);
  CHECK(std::strcmp(players[0].name, "anna") == 0);
  CHECK(std::strcmp(players[1].name, "IVAN") == 0);
  CHECK(std::strcmp(players[2].name, "zoe") == 0);
  CHECK(players[1].id.bytes.back() == 2);

  count = 99;
  CHECK(store.listPlayers(players, 2, count) == player::StoreResult::Ok);
  CHECK(count == 2);
  CHECK(std::strcmp(players[0].name, "anna") == 0);
  CHECK(std::strcmp(players[1].name, "IVAN") == 0);

  count = 99;
  CHECK(store.listPlayers(nullptr, 0, count) == player::StoreResult::Ok);
  CHECK(count == 0);
  CHECK(store.listPlayers(nullptr, 1, count) == player::StoreResult::InvalidArgument);
  CHECK(count == 0);

  store.close();
  std::remove(path.c_str());
  std::remove((path + "-journal").c_str());

  std::printf("player directory: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
