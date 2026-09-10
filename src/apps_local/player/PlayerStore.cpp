#include "PlayerStore.h"

#include <cstring>

#include <sqlite3.h>

namespace player {
namespace {

constexpr char kSchemaV1[] =
    "BEGIN IMMEDIATE;"
    "CREATE TABLE players ("
    "id BLOB PRIMARY KEY NOT NULL CHECK(length(id) = 16),"
    "name TEXT NOT NULL COLLATE NOCASE UNIQUE CHECK(length(name) BETWEEN 1 AND 20),"
    "pin_hash BLOB,"
    "pin_salt BLOB,"
    "callsign_hair INTEGER NOT NULL CHECK(callsign_hair BETWEEN 0 AND 255),"
    "callsign_eyes INTEGER NOT NULL CHECK(callsign_eyes BETWEEN 0 AND 255),"
    "callsign_mouth INTEGER NOT NULL CHECK(callsign_mouth BETWEEN 0 AND 255),"
    "created_at INTEGER NOT NULL CHECK(created_at >= 0)"
    ");"
    "CREATE TABLE game_stats ("
    "player_id BLOB NOT NULL CHECK(length(player_id) = 16),"
    "game INTEGER NOT NULL CHECK(game > 0),"
    "wins INTEGER NOT NULL DEFAULT 0 CHECK(wins >= 0),"
    "losses INTEGER NOT NULL DEFAULT 0 CHECK(losses >= 0),"
    "draws INTEGER NOT NULL DEFAULT 0 CHECK(draws >= 0),"
    "current_streak INTEGER NOT NULL DEFAULT 0 CHECK(current_streak >= 0),"
    "best_streak INTEGER NOT NULL DEFAULT 0 CHECK(best_streak >= 0),"
    "xp INTEGER NOT NULL DEFAULT 0 CHECK(xp >= 0),"
    "PRIMARY KEY(player_id, game),"
    "FOREIGN KEY(player_id) REFERENCES players(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE matches ("
    "id BLOB PRIMARY KEY NOT NULL CHECK(length(id) = 16),"
    "game INTEGER NOT NULL CHECK(game > 0),"
    "player1 BLOB CHECK(player1 IS NULL OR length(player1) = 16),"
    "player2 BLOB CHECK(player2 IS NULL OR length(player2) = 16),"
    "winner BLOB CHECK(winner IS NULL OR length(winner) = 16),"
    "result INTEGER NOT NULL,"
    "ended_at INTEGER NOT NULL CHECK(ended_at >= 0),"
    "FOREIGN KEY(player1) REFERENCES players(id) ON DELETE SET NULL,"
    "FOREIGN KEY(player2) REFERENCES players(id) ON DELETE SET NULL,"
    "FOREIGN KEY(winner) REFERENCES players(id) ON DELETE SET NULL"
    ");"
    "PRAGMA user_version = 1;"
    "COMMIT;";

bool validName(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  size_t length = 0;
  while (length <= kMaxPlayerNameLength && name[length] != '\0') ++length;
  return length > 0 && length <= kMaxPlayerNameLength;
}

int bindId(sqlite3_stmt* statement, int index, const PlayerId& id) {
  return sqlite3_bind_blob(statement, index, id.bytes.data(), static_cast<int>(id.bytes.size()), SQLITE_STATIC);
}

bool readId(sqlite3_stmt* statement, int column, PlayerId& out) {
  const void* data = sqlite3_column_blob(statement, column);
  const int size = sqlite3_column_bytes(statement, column);
  if (data == nullptr || size != static_cast<int>(PlayerId::kSize)) return false;
  std::memcpy(out.bytes.data(), data, PlayerId::kSize);
  return true;
}

bool readPlayer(sqlite3_stmt* statement, Player& out) {
  Player value{};
  if (!readId(statement, 0, value.id)) return false;

  const unsigned char* text = sqlite3_column_text(statement, 1);
  const int nameBytes = sqlite3_column_bytes(statement, 1);
  if (text == nullptr || nameBytes <= 0 || nameBytes > static_cast<int>(kMaxPlayerNameLength)) return false;
  std::memcpy(value.name, text, static_cast<size_t>(nameBytes));
  value.name[nameBytes] = '\0';

  for (int slot = 0; slot < kSlotCount; ++slot) {
    const int word = sqlite3_column_int(statement, 2 + slot);
    if (word < 0 || word > 255) return false;
    value.callsign.word[slot] = static_cast<uint8_t>(word);
  }

  const sqlite3_int64 createdAt = sqlite3_column_int64(statement, 5);
  if (createdAt < 0) return false;
  value.createdAt = static_cast<uint64_t>(createdAt);
  out = value;
  return true;
}

StoreResult prepare(sqlite3* db, const char* sql, sqlite3_stmt** statement) {
  return sqlite3_prepare_v2(db, sql, -1, statement, nullptr) == SQLITE_OK ? StoreResult::Ok : StoreResult::SqlError;
}

}  // namespace

PlayerStore::~PlayerStore() { close(); }

StoreResult PlayerStore::open(const char* path) {
  if (path == nullptr || path[0] == '\0') return StoreResult::InvalidArgument;
  close();

  if (sqlite3_open(path, &db_) != SQLITE_OK) {
    close();
    return StoreResult::SqlError;
  }

  sqlite3_extended_result_codes(db_, 1);
  sqlite3_busy_timeout(db_, 2000);
  if (sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    close();
    return StoreResult::SqlError;
  }

  int version = 0;
  StoreResult result = readSchemaVersion(version);
  if (result != StoreResult::Ok) {
    close();
    return result;
  }
  if (version > kSchemaVersion) {
    close();
    return StoreResult::UnsupportedSchema;
  }
  if (version == 0) {
    result = initializeSchema();
    if (result != StoreResult::Ok) {
      close();
      return result;
    }
    version = kSchemaVersion;
  }
  if (version != kSchemaVersion) {
    close();
    return StoreResult::UnsupportedSchema;
  }

  schemaVersion_ = version;
  return StoreResult::Ok;
}

void PlayerStore::close() {
  if (db_ != nullptr) sqlite3_close(db_);
  db_ = nullptr;
  schemaVersion_ = 0;
}

StoreResult PlayerStore::initializeSchema() {
  if (db_ == nullptr) return StoreResult::NotOpen;
  char* error = nullptr;
  const int result = sqlite3_exec(db_, kSchemaV1, nullptr, nullptr, &error);
  if (error != nullptr) sqlite3_free(error);
  if (result == SQLITE_OK) return StoreResult::Ok;
  sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
  return StoreResult::SqlError;
}

StoreResult PlayerStore::readSchemaVersion(int& version) const {
  if (db_ == nullptr) return StoreResult::NotOpen;
  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, "PRAGMA user_version;", &statement);
  if (result != StoreResult::Ok) return result;

  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) {
    version = sqlite3_column_int(statement, 0);
    result = StoreResult::Ok;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::createPlayer(const Player& value) {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (value.id.empty() || !validName(value.name) || !value.callsign.known()) return StoreResult::InvalidArgument;
  if (value.createdAt > static_cast<uint64_t>(INT64_MAX)) return StoreResult::InvalidArgument;

  constexpr char sql[] =
      "INSERT INTO players(id, name, callsign_hair, callsign_eyes, callsign_mouth, created_at) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6);";
  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, sql, &statement);
  if (result != StoreResult::Ok) return result;

  if (bindId(statement, 1, value.id) != SQLITE_OK ||
      sqlite3_bind_text(statement, 2, value.name, -1, SQLITE_STATIC) != SQLITE_OK ||
      sqlite3_bind_int(statement, 3, value.callsign.word[SlotHair]) != SQLITE_OK ||
      sqlite3_bind_int(statement, 4, value.callsign.word[SlotEyes]) != SQLITE_OK ||
      sqlite3_bind_int(statement, 5, value.callsign.word[SlotMouth]) != SQLITE_OK ||
      sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(value.createdAt)) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  const int step = sqlite3_step(statement);
  if (step == SQLITE_DONE) {
    result = StoreResult::Ok;
  } else if (sqlite3_extended_errcode(db_) == SQLITE_CONSTRAINT_UNIQUE) {
    result = StoreResult::NameTaken;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::getPlayer(const PlayerId& id, Player& out) const {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (id.empty()) return StoreResult::InvalidArgument;

  constexpr char sql[] =
      "SELECT id, name, callsign_hair, callsign_eyes, callsign_mouth, created_at "
      "FROM players WHERE id = ?1;";
  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, sql, &statement);
  if (result != StoreResult::Ok) return result;
  if (bindId(statement, 1, id) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) {
    result = readPlayer(statement, out) ? StoreResult::Ok : StoreResult::SqlError;
  } else if (step == SQLITE_DONE) {
    result = StoreResult::NotFound;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::findPlayerByName(const char* name, Player& out) const {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (!validName(name)) return StoreResult::InvalidArgument;

  constexpr char sql[] =
      "SELECT id, name, callsign_hair, callsign_eyes, callsign_mouth, created_at "
      "FROM players WHERE name = ?1 COLLATE NOCASE;";
  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, sql, &statement);
  if (result != StoreResult::Ok) return result;
  if (sqlite3_bind_text(statement, 1, name, -1, SQLITE_STATIC) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) {
    result = readPlayer(statement, out) ? StoreResult::Ok : StoreResult::SqlError;
  } else if (step == SQLITE_DONE) {
    result = StoreResult::NotFound;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::playerExists(const PlayerId& id, bool& exists) const {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (id.empty()) return StoreResult::InvalidArgument;

  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, "SELECT 1 FROM players WHERE id = ?1 LIMIT 1;", &statement);
  if (result != StoreResult::Ok) return result;
  if (bindId(statement, 1, id) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) {
    exists = true;
    result = StoreResult::Ok;
  } else if (step == SQLITE_DONE) {
    exists = false;
    result = StoreResult::Ok;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::getGameStats(const PlayerId& playerId, GameId game, GameStats& out) const {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (playerId.empty() || game == GameId::Unknown) return StoreResult::InvalidArgument;

  constexpr char sql[] =
      "SELECT wins, losses, draws, current_streak, best_streak, xp "
      "FROM game_stats WHERE player_id = ?1 AND game = ?2;";
  sqlite3_stmt* statement = nullptr;
  StoreResult result = prepare(db_, sql, &statement);
  if (result != StoreResult::Ok) return result;
  if (bindId(statement, 1, playerId) != SQLITE_OK ||
      sqlite3_bind_int(statement, 2, static_cast<int>(game)) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) {
    GameStats value{};
    value.playerId = playerId;
    value.game = game;
    value.wins = static_cast<uint32_t>(sqlite3_column_int64(statement, 0));
    value.losses = static_cast<uint32_t>(sqlite3_column_int64(statement, 1));
    value.draws = static_cast<uint32_t>(sqlite3_column_int64(statement, 2));
    value.currentStreak = static_cast<uint32_t>(sqlite3_column_int64(statement, 3));
    value.bestStreak = static_cast<uint32_t>(sqlite3_column_int64(statement, 4));
    value.xp = static_cast<uint32_t>(sqlite3_column_int64(statement, 5));
    out = value;
    result = StoreResult::Ok;
  } else if (step == SQLITE_DONE) {
    result = StoreResult::NotFound;
  } else {
    result = StoreResult::SqlError;
  }
  sqlite3_finalize(statement);
  return result;
}

StoreResult PlayerStore::saveGameStats(const GameStats& value) {
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (value.playerId.empty() || value.game == GameId::Unknown) return StoreResult::InvalidArgument;

  bool exists = false;
  StoreResult result = playerExists(value.playerId, exists);
  if (result != StoreResult::Ok) return result;
  if (!exists) return StoreResult::NotFound;

  constexpr char sql[] =
      "INSERT INTO game_stats(player_id, game, wins, losses, draws, current_streak, best_streak, xp) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
      "ON CONFLICT(player_id, game) DO UPDATE SET "
      "wins=excluded.wins, losses=excluded.losses, draws=excluded.draws, "
      "current_streak=excluded.current_streak, best_streak=excluded.best_streak, xp=excluded.xp;";
  sqlite3_stmt* statement = nullptr;
  result = prepare(db_, sql, &statement);
  if (result != StoreResult::Ok) return result;

  const bool bound =
      bindId(statement, 1, value.playerId) == SQLITE_OK &&
      sqlite3_bind_int(statement, 2, static_cast<int>(value.game)) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 3, value.wins) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 4, value.losses) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 5, value.draws) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 6, value.currentStreak) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 7, value.bestStreak) == SQLITE_OK &&
      sqlite3_bind_int64(statement, 8, value.xp) == SQLITE_OK;
  if (!bound) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  result = sqlite3_step(statement) == SQLITE_DONE ? StoreResult::Ok : StoreResult::SqlError;
  sqlite3_finalize(statement);
  return result;
}

}  // namespace player
