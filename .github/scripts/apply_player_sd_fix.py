from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))

# HalFile needs real truncation for SQLite xTruncate. Returning SQLITE_OK without
# changing the file length violates the VFS contract and can leave stale DB or
# journal tails on the SD card.
replace(
    "lib/hal/HalStorage.h",
    "  bool seekSet(size_t offset);\n  int available() const;",
    "  bool seekSet(size_t offset);\n  bool truncate(uint64_t size);\n  int available() const;",
)
replace(
    "lib/hal/HalStorage.cpp",
    "bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }\nint HalFile::available() const",
    "bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }\nbool HalFile::truncate(uint64_t size) { HAL_FILE_WRAPPED_CALL(truncate, size); }\nint HalFile::available() const",
)

# Make the custom SQLite VFS report real SD failures and implement xTruncate.
replace(
    "src/apps_local/player/PlayerSqliteVfs.cpp",
    "#include <HalStorage.h>\n#include <esp_system.h>",
    "#include <HalStorage.h>\n#include <Logging.h>\n#include <esp_system.h>",
)
replace(
    "src/apps_local/player/PlayerSqliteVfs.cpp",
    "// The SQLite library previously used by this firmware also leaves truncate as\n// a no-op on ESP32. Journals may retain tail bytes, but SQLite tracks the valid\n// journal/database length itself. Keeping the same behavior avoids reaching\n// through HalStorage into SdFat internals just for truncate().\nint fileTruncate(sqlite3_file*, sqlite3_int64) { return SQLITE_OK; }",
    "int fileTruncate(sqlite3_file* raw, sqlite3_int64 size) {\n  if (size < 0) return SQLITE_IOERR_TRUNCATE;\n  PlayerSqliteFile* file = asFile(raw);\n  if (!file->file.truncate(static_cast<uint64_t>(size))) {\n    LOG_ERR(\"PLAYER_DB\", \"truncate failed path=%s size=%lld\", file->path, static_cast<long long>(size));\n    return SQLITE_IOERR_TRUNCATE;\n  }\n  return SQLITE_OK;\n}",
)
replace(
    "src/apps_local/player/PlayerSqliteVfs.cpp",
    "  file->file = Storage.open(file->path, openFlags);\n  if (!file->file) {\n    file->~PlayerSqliteFile();\n    return SQLITE_CANTOPEN;\n  }",
    "  file->file = Storage.open(file->path, openFlags);\n  if (!file->file) {\n    LOG_ERR(\"PLAYER_DB\", \"open failed path=%s sqlite_flags=0x%x sd_flags=0x%x\", file->path, flags,\n            static_cast<unsigned int>(openFlags));\n    file->~PlayerSqliteFile();\n    return SQLITE_CANTOPEN;\n  }",
)
replace(
    "src/apps_local/player/PlayerSqliteVfs.cpp",
    "int vfsDelete(sqlite3_vfs*, const char* path, int) {\n  if (path == nullptr) return SQLITE_IOERR_DELETE;\n  if (!Storage.exists(path)) return SQLITE_OK;\n  return Storage.remove(path) ? SQLITE_OK : SQLITE_IOERR_DELETE;\n}",
    "int vfsDelete(sqlite3_vfs*, const char* path, int) {\n  if (path == nullptr) return SQLITE_IOERR_DELETE;\n  if (!Storage.exists(path)) return SQLITE_OK;\n  if (Storage.remove(path)) return SQLITE_OK;\n  LOG_ERR(\"PLAYER_DB\", \"delete failed path=%s\", path);\n  return SQLITE_IOERR_DELETE;\n}",
)

# A broken persistent store must not make the whole player UX unusable. Keep a
# RAM guest alive, while making persistence capability explicit.
replace(
    "src/apps_local/player/PlayerRuntime.h",
    "  DatabaseError,\n  GuestError,",
    "  DatabaseError,\n  GuestOnly,\n  GuestError,",
)
replace(
    "src/apps_local/player/PlayerRuntime.h",
    "  bool ready() const { return status_ == RuntimeStatus::Ready; }\n  RuntimeStatus status() const { return status_; }",
    "  bool ready() const { return status_ == RuntimeStatus::Ready || status_ == RuntimeStatus::GuestOnly; }\n  bool persistenceReady() const { return status_ == RuntimeStatus::Ready; }\n  RuntimeStatus status() const { return status_; }",
)

replace(
    "src/apps_local/player/PlayerRuntime.cpp",
    "#include <HalStorage.h>\n#include <esp_system.h>",
    "#include <HalStorage.h>\n#include <Logging.h>\n#include <esp_system.h>",
)
replace(
    "src/apps_local/player/PlayerRuntime.cpp",
    "  if (openResult != StoreResult::Ok) {\n    store_.close();\n    status_ = RuntimeStatus::DatabaseError;\n    return false;\n  }",
    "  if (openResult != StoreResult::Ok) {\n    store_.close();\n#if !defined(SIMULATOR)\n    LOG_ERR(\"PLAYER_DB\", \"PlayerStore open/schema failed result=%u path=%s\",\n            static_cast<unsigned int>(openResult), kDatabasePath);\n#endif\n    if (ensureGuest() == PlayerServiceResult::Ok) {\n      status_ = RuntimeStatus::GuestOnly;\n      return true;\n    }\n    status_ = RuntimeStatus::DatabaseError;\n    return false;\n  }",
)
replace(
    "src/apps_local/player/PlayerRuntime.cpp",
    "PlayerServiceResult PlayerRuntime::registerGuest(const char* name, const char* pin, const uint64_t createdAt) {\n  if (!ready()) return PlayerServiceResult::StorageError;",
    "PlayerServiceResult PlayerRuntime::registerGuest(const char* name, const char* pin, const uint64_t createdAt) {\n  if (!persistenceReady()) return PlayerServiceResult::StorageError;",
)
replace(
    "src/apps_local/player/PlayerRuntime.cpp",
    "StoreResult PlayerRuntime::listPlayers(Player* out, const size_t capacity, size_t& count) const {\n  if (!ready()) {\n    count = 0;\n    return StoreResult::NotOpen;\n  }\n  return store_.listPlayers(out, capacity, count);\n}",
    "StoreResult PlayerRuntime::listPlayers(Player* out, const size_t capacity, size_t& count) const {\n  if (!ready()) {\n    count = 0;\n    return StoreResult::NotOpen;\n  }\n  if (!persistenceReady()) {\n    count = 0;\n    return StoreResult::Ok;\n  }\n  return store_.listPlayers(out, capacity, count);\n}",
)

# Surface degraded mode instead of the misleading PLAYER ERROR state.
replace(
    "src/apps_local/player/PlayerActivity.cpp",
    "  if (!player::runtime().begin()) {\n    setMessage(\"PLAYER DATABASE COULD NOT OPEN\");\n  }\n  refreshPlayers();",
    "  if (!player::runtime().begin()) {\n    setMessage(\"PLAYER RUNTIME COULD NOT START\");\n  } else if (!player::runtime().persistenceReady()) {\n    setMessage(\"STORAGE OFFLINE - GUEST ONLY\");\n  }\n  refreshPlayers();",
)
replace(
    "src/apps_local/player/PlayerActivity.cpp",
    "    model.canRegisterGuest = guestAvailable && guestSelected && model.guestCompletedMatches > 0;",
    "    model.canRegisterGuest = player::runtime().persistenceReady() && guestAvailable && guestSelected &&\n                             model.guestCompletedMatches > 0;",
)

# Remove the temporary trigger marker from the feature branch when this patch is
# committed.
marker = Path(".tmp-player-fix-marker")
if marker.exists():
    marker.unlink()
