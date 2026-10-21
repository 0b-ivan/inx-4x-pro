#include "PlayerStore.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <HalStorage.h>
#endif

namespace player {
namespace {

constexpr uint8_t kMagic[4] = {'X', '4', 'P', 'L'};
constexpr size_t kHeaderSize = 16;
constexpr size_t kRecordSize = 323;
constexpr size_t kMaxFileSize = kHeaderSize + PlayerStore::kMaxPlayers * kRecordSize;

uint32_t checksum(const uint8_t* data, const size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

void put16(uint8_t*& out, const uint16_t value) {
  *out++ = static_cast<uint8_t>(value & 0xffu);
  *out++ = static_cast<uint8_t>((value >> 8u) & 0xffu);
}

void put32(uint8_t*& out, const uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) *out++ = static_cast<uint8_t>((value >> shift) & 0xffu);
}

void put64(uint8_t*& out, const uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) *out++ = static_cast<uint8_t>((value >> shift) & 0xffu);
}

uint16_t get16(const uint8_t*& in) {
  const uint16_t value = static_cast<uint16_t>(in[0]) | static_cast<uint16_t>(in[1] << 8u);
  in += 2;
  return value;
}

uint32_t get32(const uint8_t*& in) {
  uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) value |= static_cast<uint32_t>(*in++) << shift;
  return value;
}

uint64_t get64(const uint8_t*& in) {
  uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) value |= static_cast<uint64_t>(*in++) << shift;
  return value;
}

bool validName(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  size_t length = 0;
  while (length <= kMaxPlayerNameLength && name[length] != '\0') ++length;
  return length > 0 && length <= kMaxPlayerNameLength;
}

bool validPlayer(const Player& value) {
  return !value.id.empty() && validName(value.name) && value.callsign.known();
}

bool equalNoCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    const unsigned char a = static_cast<unsigned char>(*left++);
    const unsigned char b = static_cast<unsigned char>(*right++);
    if (std::tolower(a) != std::tolower(b)) return false;
  }
  return *left == *right;
}

int gameIndex(const GameId game) {
  const int value = static_cast<int>(game);
  return value >= 1 && value <= static_cast<int>(PlayerStore::kGameCount) ? value - 1 : -1;
}

bool fileExists(const char* path) {
#if defined(ARDUINO)
  return Storage.exists(path);
#else
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) return false;
  std::fclose(file);
  return true;
#endif
}

bool readWhole(const char* path, uint8_t* data, const size_t capacity, size_t& size) {
  size = 0;
#if defined(ARDUINO)
  HalFile file;
  if (!Storage.openFileForRead("PLAYER_STORE", path, file)) return false;
  const uint64_t length = file.fileSize64();
  if (length > capacity) {
    file.close();
    return false;
  }
  while (size < static_cast<size_t>(length)) {
    const int got = file.read(data + size, static_cast<size_t>(length) - size);
    if (got <= 0) {
      file.close();
      return false;
    }
    size += static_cast<size_t>(got);
  }
  file.close();
  return true;
#else
  FILE* file = std::fopen(path, "rb");
  if (file == nullptr) return false;
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  const long length = std::ftell(file);
  if (length < 0 || static_cast<size_t>(length) > capacity || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  size = std::fread(data, 1, static_cast<size_t>(length), file);
  const bool ok = size == static_cast<size_t>(length) && std::ferror(file) == 0;
  std::fclose(file);
  return ok;
#endif
}

bool writeWhole(const char* path, const uint8_t* data, const size_t size) {
#if defined(ARDUINO)
  HalFile file;
  if (!Storage.openFileForWrite("PLAYER_STORE", path, file)) return false;
  const bool ok = file.write(data, size) == size;
  file.flush();
  file.close();
  return ok;
#else
  FILE* file = std::fopen(path, "wb");
  if (file == nullptr) return false;
  const bool ok = std::fwrite(data, 1, size, file) == size && std::fflush(file) == 0;
  std::fclose(file);
  return ok;
#endif
}

bool removeFile(const char* path) {
#if defined(ARDUINO)
  return !Storage.exists(path) || Storage.remove(path);
#else
  return std::remove(path) == 0 || !fileExists(path);
#endif
}

bool renameFile(const char* from, const char* to) {
#if defined(ARDUINO)
  return Storage.rename(from, to);
#else
  return std::rename(from, to) == 0;
#endif
}

}  // namespace

PlayerStore::~PlayerStore() { close(); }

StoreResult PlayerStore::open(const char* path) {
  if (path == nullptr || path[0] == '\0') return StoreResult::InvalidArgument;
  close();
  if (std::strlen(path) >= path_.size()) return StoreResult::InvalidArgument;
  std::snprintf(path_.data(), path_.size(), "%s", path);
  memoryOnly_ = std::strcmp(path, ":memory:") == 0;
  open_ = true;
  schemaVersion_ = kSchemaVersion;
  if (memoryOnly_ || !fileExists(path_.data())) return StoreResult::Ok;

  const StoreResult result = load();
  if (result != StoreResult::Ok) close();
  return result;
}

void PlayerStore::close() {
  players_ = {};
  playerCount_ = 0;
  path_.fill('\0');
  memoryOnly_ = false;
  open_ = false;
  schemaVersion_ = 0;
}

int PlayerStore::findIndex(const PlayerId& id) const {
  for (size_t i = 0; i < playerCount_; ++i) {
    if (players_[i].player.id == id) return static_cast<int>(i);
  }
  return -1;
}

int PlayerStore::findNameIndex(const char* name) const {
  for (size_t i = 0; i < playerCount_; ++i) {
    if (equalNoCase(players_[i].player.name, name)) return static_cast<int>(i);
  }
  return -1;
}

StoreResult PlayerStore::load() {
  std::array<uint8_t, kMaxFileSize> bytes{};
  size_t size = 0;
  if (!readWhole(path_.data(), bytes.data(), bytes.size(), size)) return StoreResult::SqlError;
  if (size < kHeaderSize || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return StoreResult::SqlError;

  const uint8_t* header = bytes.data() + 4;
  const uint16_t version = get16(header);
  const uint16_t count = get16(header);
  const uint32_t payloadSize = get32(header);
  const uint32_t expectedChecksum = get32(header);
  if (version != kSchemaVersion) return StoreResult::UnsupportedSchema;
  if (count > kMaxPlayers || payloadSize != static_cast<uint32_t>(count) * kRecordSize ||
      size != kHeaderSize + payloadSize) {
    return StoreResult::SqlError;
  }
  const uint8_t* payload = bytes.data() + kHeaderSize;
  if (checksum(payload, payloadSize) != expectedChecksum) return StoreResult::SqlError;

  std::array<StoredPlayer, kMaxPlayers> loaded{};
  const uint8_t* in = payload;
  for (size_t i = 0; i < count; ++i) {
    StoredPlayer& record = loaded[i];
    std::memcpy(record.player.id.bytes.data(), in, PlayerId::kSize);
    in += PlayerId::kSize;
    std::memcpy(record.player.name, in, kMaxPlayerNameLength + 1);
    in += kMaxPlayerNameLength + 1;
    for (int slot = 0; slot < kSlotCount; ++slot) record.player.callsign.word[slot] = *in++;
    record.player.createdAt = get64(in);
    record.hasCredential = *in++ != 0;
    record.credential.version = static_cast<PinKdfVersion>(*in++);
    std::memcpy(record.credential.salt.data(), in, record.credential.salt.size());
    in += record.credential.salt.size();
    std::memcpy(record.credential.hash.data(), in, record.credential.hash.size());
    in += record.credential.hash.size();

    if (!validPlayer(record.player) || (record.hasCredential && !record.credential.supported())) return StoreResult::SqlError;
    for (size_t game = 0; game < kGameCount; ++game) {
      record.hasStats[game] = *in++ != 0;
      GameStats& stats = record.stats[game];
      stats.playerId = record.player.id;
      stats.game = static_cast<GameId>(game + 1);
      stats.wins = get32(in);
      stats.losses = get32(in);
      stats.draws = get32(in);
      stats.currentStreak = get32(in);
      stats.bestStreak = get32(in);
      stats.xp = get32(in);
    }
    for (size_t previous = 0; previous < i; ++previous) {
      if (loaded[previous].player.id == record.player.id ||
          equalNoCase(loaded[previous].player.name, record.player.name)) {
        return StoreResult::SqlError;
      }
    }
  }

  players_ = loaded;
  playerCount_ = count;
  schemaVersion_ = version;
  return StoreResult::Ok;
}

StoreResult PlayerStore::persist() {
  if (!open_) return StoreResult::NotOpen;
  if (memoryOnly_) return StoreResult::Ok;

  std::array<uint8_t, kMaxFileSize> bytes{};
  uint8_t* payload = bytes.data() + kHeaderSize;
  uint8_t* out = payload;
  for (size_t i = 0; i < playerCount_; ++i) {
    const StoredPlayer& record = players_[i];
    std::memcpy(out, record.player.id.bytes.data(), PlayerId::kSize);
    out += PlayerId::kSize;
    std::memcpy(out, record.player.name, kMaxPlayerNameLength + 1);
    out += kMaxPlayerNameLength + 1;
    for (int slot = 0; slot < kSlotCount; ++slot) *out++ = record.player.callsign.word[slot];
    put64(out, record.player.createdAt);
    *out++ = record.hasCredential ? 1 : 0;
    *out++ = static_cast<uint8_t>(record.credential.version);
    std::memcpy(out, record.credential.salt.data(), record.credential.salt.size());
    out += record.credential.salt.size();
    std::memcpy(out, record.credential.hash.data(), record.credential.hash.size());
    out += record.credential.hash.size();
    for (size_t game = 0; game < kGameCount; ++game) {
      *out++ = record.hasStats[game] ? 1 : 0;
      const GameStats& stats = record.stats[game];
      put32(out, stats.wins);
      put32(out, stats.losses);
      put32(out, stats.draws);
      put32(out, stats.currentStreak);
      put32(out, stats.bestStreak);
      put32(out, stats.xp);
    }
  }

  const uint32_t payloadSize = static_cast<uint32_t>(out - payload);
  uint8_t* header = bytes.data();
  std::memcpy(header, kMagic, sizeof(kMagic));
  header += sizeof(kMagic);
  put16(header, kSchemaVersion);
  put16(header, static_cast<uint16_t>(playerCount_));
  put32(header, payloadSize);
  put32(header, checksum(payload, payloadSize));

  char temp[144]{};
  char backup[144]{};
  std::snprintf(temp, sizeof(temp), "%s.tmp", path_.data());
  std::snprintf(backup, sizeof(backup), "%s.bak", path_.data());
  removeFile(temp);
  removeFile(backup);
  if (!writeWhole(temp, bytes.data(), kHeaderSize + payloadSize)) {
    removeFile(temp);
    return StoreResult::SqlError;
  }

  const bool hadOriginal = fileExists(path_.data());
  if (hadOriginal && !renameFile(path_.data(), backup)) {
    removeFile(temp);
    return StoreResult::SqlError;
  }
  if (!renameFile(temp, path_.data())) {
    if (hadOriginal) renameFile(backup, path_.data());
    removeFile(temp);
    return StoreResult::SqlError;
  }
  if (hadOriginal) removeFile(backup);
  return StoreResult::Ok;
}

StoreResult PlayerStore::createPlayer(const Player& value) {
  if (!open_) return StoreResult::NotOpen;
  if (!validPlayer(value)) return StoreResult::InvalidArgument;
  if (findNameIndex(value.name) >= 0) return StoreResult::NameTaken;
  if (findIndex(value.id) >= 0 || playerCount_ >= kMaxPlayers) return StoreResult::SqlError;

  const auto backup = players_;
  const size_t backupCount = playerCount_;
  players_[playerCount_].player = value;
  ++playerCount_;
  const StoreResult result = persist();
  if (result != StoreResult::Ok) {
    players_ = backup;
    playerCount_ = backupCount;
  }
  return result;
}

StoreResult PlayerStore::createRegisteredPlayer(const Player& value, const PinCredential& credential,
                                                const GameStats* stats, const size_t statsCount) {
  if (!open_) return StoreResult::NotOpen;
  if (!validPlayer(value) || !credential.supported() || (statsCount > 0 && stats == nullptr)) {
    return StoreResult::InvalidArgument;
  }
  if (findNameIndex(value.name) >= 0) return StoreResult::NameTaken;
  if (findIndex(value.id) >= 0 || playerCount_ >= kMaxPlayers) return StoreResult::SqlError;

  StoredPlayer record{};
  record.player = value;
  record.hasCredential = true;
  record.credential = credential;
  for (size_t i = 0; i < statsCount; ++i) {
    const int index = gameIndex(stats[i].game);
    if (stats[i].playerId != value.id || index < 0) return StoreResult::InvalidArgument;
    record.stats[index] = stats[i];
    record.hasStats[index] = true;
  }

  const auto backup = players_;
  const size_t backupCount = playerCount_;
  players_[playerCount_++] = record;
  const StoreResult result = persist();
  if (result != StoreResult::Ok) {
    players_ = backup;
    playerCount_ = backupCount;
  }
  return result;
}

StoreResult PlayerStore::getPlayer(const PlayerId& id, Player& out) const {
  if (!open_) return StoreResult::NotOpen;
  if (id.empty()) return StoreResult::InvalidArgument;
  const int index = findIndex(id);
  if (index < 0) return StoreResult::NotFound;
  out = players_[index].player;
  return StoreResult::Ok;
}

StoreResult PlayerStore::findPlayerByName(const char* name, Player& out) const {
  if (!open_) return StoreResult::NotOpen;
  if (!validName(name)) return StoreResult::InvalidArgument;
  const int index = findNameIndex(name);
  if (index < 0) return StoreResult::NotFound;
  out = players_[index].player;
  return StoreResult::Ok;
}

StoreResult PlayerStore::getPinCredential(const PlayerId& id, PinCredential& out) const {
  if (!open_) return StoreResult::NotOpen;
  if (id.empty()) return StoreResult::InvalidArgument;
  const int index = findIndex(id);
  if (index < 0) return StoreResult::NotFound;
  if (!players_[index].hasCredential) return StoreResult::CredentialMissing;
  out = players_[index].credential;
  return StoreResult::Ok;
}

StoreResult PlayerStore::listPlayers(Player* out, const size_t capacity, size_t& count) const {
  count = 0;
  if (!open_) return StoreResult::NotOpen;
  if (capacity > 0 && out == nullptr) return StoreResult::InvalidArgument;

  std::array<size_t, kMaxPlayers> order{};
  for (size_t i = 0; i < playerCount_; ++i) order[i] = i;
  for (size_t i = 1; i < playerCount_; ++i) {
    const size_t current = order[i];
    size_t j = i;
    while (j > 0) {
      const char* left = players_[order[j - 1]].player.name;
      const char* right = players_[current].player.name;
      int cmp = 0;
      for (size_t at = 0;; ++at) {
        const unsigned char a = static_cast<unsigned char>(left[at]);
        const unsigned char b = static_cast<unsigned char>(right[at]);
        const int lowerA = std::tolower(a);
        const int lowerB = std::tolower(b);
        if (lowerA != lowerB) { cmp = lowerA < lowerB ? -1 : 1; break; }
        if (a == 0 || b == 0) break;
      }
      if (cmp <= 0) break;
      order[j] = order[j - 1];
      --j;
    }
    order[j] = current;
  }

  const size_t outputCount = playerCount_ < capacity ? playerCount_ : capacity;
  for (size_t i = 0; i < outputCount; ++i) out[i] = players_[order[i]].player;
  count = outputCount;
  return StoreResult::Ok;
}

StoreResult PlayerStore::getGameStats(const PlayerId& playerId, const GameId game, GameStats& out) const {
  if (!open_) return StoreResult::NotOpen;
  const int player = findIndex(playerId);
  const int slot = gameIndex(game);
  if (player < 0) return StoreResult::NotFound;
  if (slot < 0) return StoreResult::InvalidArgument;
  if (!players_[player].hasStats[slot]) return StoreResult::NotFound;
  out = players_[player].stats[slot];
  return StoreResult::Ok;
}

StoreResult PlayerStore::saveGameStats(const GameStats& value) {
  return saveGameStatsBatch(&value, 1);
}

StoreResult PlayerStore::saveGameStatsBatch(const GameStats* values, const size_t count) {
  if (!open_) return StoreResult::NotOpen;
  if (count > 0 && values == nullptr) return StoreResult::InvalidArgument;
  if (count == 0) return StoreResult::Ok;

  for (size_t i = 0; i < count; ++i) {
    if (values[i].playerId.empty() || gameIndex(values[i].game) < 0) return StoreResult::InvalidArgument;
    if (!playerExists(values[i].playerId)) return StoreResult::NotFound;
  }

  const auto backup = players_;
  for (size_t i = 0; i < count; ++i) {
    const int player = findIndex(values[i].playerId);
    const int slot = gameIndex(values[i].game);
    players_[player].stats[slot] = values[i];
    players_[player].hasStats[slot] = true;
  }
  const StoreResult result = persist();
  if (result != StoreResult::Ok) players_ = backup;
  return result;
}

}  // namespace player
