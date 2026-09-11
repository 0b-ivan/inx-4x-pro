#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "GameStats.h"
#include "PinCredential.h"
#include "Player.h"

namespace player {

// Storage errors stay explicit because the firmware is built without exceptions.
// SqlError is retained for API compatibility with the first SQLite-backed alpha;
// it now means a generic persistent-store I/O or integrity error.
enum class StoreResult : uint8_t {
  Ok = 0,
  NotOpen,
  InvalidArgument,
  NameTaken,
  NotFound,
  CredentialMissing,
  SqlError,
  UnsupportedSchema,
};

// Small fixed-capacity player store persisted as one versioned binary file.
// The in-memory shape is bounded and allocation-free. Mutations are committed
// through a temporary file and rename, so callers keep the same transactional
// semantics the old SQLite implementation exposed without needing SQLite/VFS.
class PlayerStore {
 public:
  static constexpr int kSchemaVersion = 1;
  static constexpr size_t kMaxPlayers = 8;
  static constexpr size_t kGameCount = 9;

  PlayerStore() = default;
  ~PlayerStore();

  PlayerStore(const PlayerStore&) = delete;
  PlayerStore& operator=(const PlayerStore&) = delete;

  StoreResult open(const char* path);
  void close();

  bool isOpen() const { return open_; }
  int schemaVersion() const { return schemaVersion_; }

  StoreResult createPlayer(const Player& value);
  StoreResult createRegisteredPlayer(const Player& value, const PinCredential& credential,
                                     const GameStats* stats, size_t statsCount);
  StoreResult getPlayer(const PlayerId& id, Player& out) const;
  StoreResult findPlayerByName(const char* name, Player& out) const;
  StoreResult getPinCredential(const PlayerId& id, PinCredential& out) const;

  StoreResult listPlayers(Player* out, size_t capacity, size_t& count) const;

  StoreResult getGameStats(const PlayerId& playerId, GameId game, GameStats& out) const;
  StoreResult saveGameStats(const GameStats& value);
  StoreResult saveGameStatsBatch(const GameStats* values, size_t count);

 private:
  struct StoredPlayer {
    Player player{};
    bool hasCredential = false;
    PinCredential credential{};
    std::array<GameStats, kGameCount> stats{};
    std::array<bool, kGameCount> hasStats{};
  };

  StoreResult load();
  StoreResult persist();
  int findIndex(const PlayerId& id) const;
  int findNameIndex(const char* name) const;
  bool playerExists(const PlayerId& id) const { return findIndex(id) >= 0; }

  std::array<StoredPlayer, kMaxPlayers> players_{};
  size_t playerCount_ = 0;
  std::array<char, 128> path_{};
  bool memoryOnly_ = false;
  bool open_ = false;
  int schemaVersion_ = 0;
};

}  // namespace player
