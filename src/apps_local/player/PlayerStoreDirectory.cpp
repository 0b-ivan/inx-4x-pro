#include "PlayerStore.h"

#include <climits>
#include <cstring>

#include <sqlite3.h>

namespace player {
namespace {

bool readPlayerDirectoryRow(sqlite3_stmt* statement, Player& out) {
  Player value{};

  const void* id = sqlite3_column_blob(statement, 0);
  const int idBytes = sqlite3_column_bytes(statement, 0);
  if (id == nullptr || idBytes != static_cast<int>(PlayerId::kSize)) return false;
  std::memcpy(value.id.bytes.data(), id, PlayerId::kSize);

  const unsigned char* name = sqlite3_column_text(statement, 1);
  const int nameBytes = sqlite3_column_bytes(statement, 1);
  if (name == nullptr || nameBytes <= 0 || nameBytes > static_cast<int>(kMaxPlayerNameLength)) return false;
  std::memcpy(value.name, name, static_cast<size_t>(nameBytes));
  value.name[nameBytes] = '\0';

  for (int slot = 0; slot < kSlotCount; ++slot) {
    const int word = sqlite3_column_int(statement, 2 + slot);
    if (word < 0 || word > 255) return false;
    value.callsign.word[slot] = static_cast<uint8_t>(word);
  }
  if (!value.callsign.known()) return false;

  const sqlite3_int64 createdAt = sqlite3_column_int64(statement, 5);
  if (createdAt < 0) return false;
  value.createdAt = static_cast<uint64_t>(createdAt);

  out = value;
  return true;
}

}  // namespace

StoreResult PlayerStore::listPlayers(Player* out, const size_t capacity, size_t& count) const {
  count = 0;
  if (db_ == nullptr) return StoreResult::NotOpen;
  if (capacity == 0) return StoreResult::Ok;
  if (out == nullptr || capacity > static_cast<size_t>(INT_MAX)) return StoreResult::InvalidArgument;

  constexpr char sql[] =
      "SELECT id, name, callsign_hair, callsign_eyes, callsign_mouth, created_at "
      "FROM players ORDER BY name COLLATE NOCASE, created_at, id LIMIT ?1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) return StoreResult::SqlError;
  if (sqlite3_bind_int(statement, 1, static_cast<int>(capacity)) != SQLITE_OK) {
    sqlite3_finalize(statement);
    return StoreResult::SqlError;
  }

  StoreResult result = StoreResult::Ok;
  while (count < capacity) {
    const int step = sqlite3_step(statement);
    if (step == SQLITE_DONE) break;
    if (step != SQLITE_ROW || !readPlayerDirectoryRow(statement, out[count])) {
      result = StoreResult::SqlError;
      break;
    }
    ++count;
  }

  sqlite3_finalize(statement);
  return result;
}

}  // namespace player
