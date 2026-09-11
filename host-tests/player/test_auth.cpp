#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#include "../../src/apps_local/player/PlayerAuth.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (condition) return;
  ++checksFailed;
  std::printf("FAIL test_auth.cpp:%d  %s\n", line, what);
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
  for (size_t index = 0; index < id.bytes.size(); ++index) id.bytes[index] = static_cast<uint8_t>(first + index);
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

std::string tempDatabase() {
  return std::string("/tmp/inx-player-auth-") + std::to_string(static_cast<long long>(getpid())) + ".db";
}

void removeDatabase(const std::string& path) {
  std::remove(path.c_str());
  std::remove((path + "-journal").c_str());
}

}  // namespace

int main() {
  CHECK(player::PlayerAuth::validPin("0000"));
  CHECK(player::PlayerAuth::validPin("9876"));
  CHECK(!player::PlayerAuth::validPin("123"));
  CHECK(!player::PlayerAuth::validPin("12345"));
  CHECK(!player::PlayerAuth::validPin("12a4"));
  CHECK(!player::PlayerAuth::validPin(nullptr));

  const std::string path = tempDatabase();
  removeDatabase(path);
  player::PlayerStore store;
  CHECK(store.open(path.c_str()) == player::StoreResult::Ok);

  RandomState random{};
  player::PlayerAuth auth(store, deterministicRandom, &random);
  player::PinCredential first{};
  player::PinCredential second{};
  CHECK(auth.createCredential("1234", first) == player::PinHashResult::Ok);
  CHECK(auth.createCredential("1234", second) == player::PinHashResult::Ok);
  CHECK(first.salt != second.salt);
  CHECK(first.hash != second.hash);
  CHECK(first.supported());

  player::Player ivan = makePlayer(10, "IVAN");
  CHECK(store.createRegisteredPlayer(ivan, first, nullptr, 0) == player::StoreResult::Ok);

  player::PinCredential persisted{};
  CHECK(store.getPinCredential(ivan.id, persisted) == player::StoreResult::Ok);
  CHECK(persisted.version == first.version);
  CHECK(persisted.salt == first.salt);
  CHECK(persisted.hash == first.hash);
  CHECK(auth.authenticate(ivan.id, "1234") == player::AuthResult::Success);

  for (int attempt = 0; attempt < 4; ++attempt) {
    CHECK(auth.authenticate(ivan.id, "0000") == player::AuthResult::WrongPin);
  }
  CHECK(auth.authenticate(ivan.id, "0000") == player::AuthResult::Locked);
  CHECK(auth.authenticate(ivan.id, "1234") == player::AuthResult::Locked);

  // Lockout is intentionally RAM-only for the first implementation.
  player::PlayerAuth afterReboot(store, deterministicRandom, &random);
  CHECK(afterReboot.authenticate(ivan.id, "1234") == player::AuthResult::Success);
  CHECK(afterReboot.authenticate(idFrom(200), "1234") == player::AuthResult::PlayerNotFound);
  CHECK(afterReboot.authenticate(ivan.id, "12x4") == player::AuthResult::InvalidArgument);

  player::Player legacy = makePlayer(80, "LUCA");
  CHECK(store.createPlayer(legacy) == player::StoreResult::Ok);
  CHECK(afterReboot.authenticate(legacy.id, "1234") == player::AuthResult::CredentialMissing);

  player::PlayerAuth noEntropy(store, zeroRandom);
  player::PinCredential unusable{};
  CHECK(noEntropy.createCredential("1234", unusable) == player::PinHashResult::RandomUnavailable);
  CHECK(noEntropy.createCredential("abc1", unusable) == player::PinHashResult::InvalidPin);

  store.close();
  removeDatabase(path);
  std::printf("player auth: %d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
