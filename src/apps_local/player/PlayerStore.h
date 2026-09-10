#pragma once

#include <cstddef>
#include <cstdint>

#include "GameStats.h"
#include "Player.h"

struct sqlite3;

namespace player {

// Storage errors are explicit because the firmware is built without exceptions.
enum class StoreResult : uint8_t {
  Ok = 0,
  NotOpen,
  InvalidArgument,
  NameTaken,
  NotFound,
  SqlError,
  UnsupportedSchema,
};

// The shared player database. The caller owns the filesystem and passes the
// SQLite VFS path to open(), which keeps this class identical on-device and in
// host tests. One PlayerService will own one PlayerStore; callers must not write
// through the same database concurrently.
class PlayerStore {
 public:
  static constexpr int kSchemaVersion = 1;

  PlayerStore() = default;
  ~PlayerStore();

  PlayerStore(const PlayerStore&) = delete;
  PlayerStore& operator=(const PlayerStore&) = delete;

  StoreResult open(const char* path);
  void close();

  bool isOpen() const { return db_ != nullptr; }
  int schemaVersion() const { return schemaVersion_; }

  StoreResult createPlayer(const Player& value);
  StoreResult getPlayer(const PlayerId& id, Player& out) const;
  StoreResult findPlayerByName(const char* name, Player& out) const;

  StoreResult getGameStats(const PlayerId& playerId, GameId game, GameStats& out) const;
  StoreResult saveGameStats(const GameStats& value);

  // Saves a whole match's participant updates as one transaction. Either every
  // GameStats row lands or none does, so a failed second write cannot award XP
  // to only one side of a match.
  StoreResult saveGameStatsBatch(const GameStats* values, size_t count);

 private:
  StoreResult initializeSchema();
  StoreResult readSchemaVersion(int& version) const;
  StoreResult playerExists(const PlayerId& id, bool& exists) const;

  sqlite3* db_ = nullptr;
  int schemaVersion_ = 0;
};

}  // namespace player
