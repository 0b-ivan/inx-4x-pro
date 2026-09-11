#include "PlayerSqliteVfs.h"

#if defined(SIMULATOR)

namespace player {

bool registerPlayerSqliteVfs() { return true; }

}  // namespace player

#else

#include <Arduino.h>
#include <HalStorage.h>
#include <esp_system.h>
#include <sqlite3.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

namespace player {
namespace {

constexpr char kVfsName[] = "freeink-sd";
constexpr size_t kPathCapacity = 160;
constexpr int kSectorSize = 512;

struct PlayerSqliteFile {
  sqlite3_file base{};
  HalFile file{};
  bool deleteOnClose = false;
  char path[kPathCapacity]{};
};

PlayerSqliteFile* asFile(sqlite3_file* file) { return reinterpret_cast<PlayerSqliteFile*>(file); }

int fileClose(sqlite3_file* raw) {
  PlayerSqliteFile* file = asFile(raw);
  file->file.flush();
  file->file.close();
  if (file->deleteOnClose && file->path[0] != '\0') Storage.remove(file->path);
  file->~PlayerSqliteFile();
  return SQLITE_OK;
}

int fileRead(sqlite3_file* raw, void* buffer, int amount, sqlite3_int64 offset) {
  PlayerSqliteFile* file = asFile(raw);
  if (amount < 0 || offset < 0 || !file->file.seek64(static_cast<uint64_t>(offset))) return SQLITE_IOERR_SEEK;

  const int read = file->file.read(buffer, static_cast<size_t>(amount));
  if (read < 0) return SQLITE_IOERR_READ;
  if (read == amount) return SQLITE_OK;

  std::memset(static_cast<uint8_t*>(buffer) + read, 0, static_cast<size_t>(amount - read));
  return SQLITE_IOERR_SHORT_READ;
}

int fileWrite(sqlite3_file* raw, const void* buffer, int amount, sqlite3_int64 offset) {
  PlayerSqliteFile* file = asFile(raw);
  if (amount < 0 || offset < 0 || !file->file.seek64(static_cast<uint64_t>(offset))) return SQLITE_IOERR_SEEK;
  return file->file.write(buffer, static_cast<size_t>(amount)) == static_cast<size_t>(amount) ? SQLITE_OK
                                                                                             : SQLITE_IOERR_WRITE;
}

// The SQLite library previously used by this firmware also leaves truncate as
// a no-op on ESP32. Journals may retain tail bytes, but SQLite tracks the valid
// journal/database length itself. Keeping the same behavior avoids reaching
// through HalStorage into SdFat internals just for truncate().
int fileTruncate(sqlite3_file*, sqlite3_int64) { return SQLITE_OK; }

int fileSync(sqlite3_file* raw, int) {
  asFile(raw)->file.flush();
  return SQLITE_OK;
}

int fileSize(sqlite3_file* raw, sqlite3_int64* size) {
  if (size == nullptr) return SQLITE_IOERR_FSTAT;
  *size = static_cast<sqlite3_int64>(asFile(raw)->file.fileSize64());
  return SQLITE_OK;
}

// PlayerStore is deliberately a single-writer service. There is no second
// SQLite connection on the device, so process-level file locking would add
// complexity without protecting a real concurrent writer.
int fileLock(sqlite3_file*, int) { return SQLITE_OK; }
int fileUnlock(sqlite3_file*, int) { return SQLITE_OK; }
int fileCheckReservedLock(sqlite3_file*, int* reserved) {
  if (reserved != nullptr) *reserved = 0;
  return SQLITE_OK;
}
int fileControl(sqlite3_file*, int, void*) { return SQLITE_NOTFOUND; }
int fileSectorSize(sqlite3_file*) { return kSectorSize; }
int fileDeviceCharacteristics(sqlite3_file*) { return 0; }

const sqlite3_io_methods kIoMethods = {
    1,
    fileClose,
    fileRead,
    fileWrite,
    fileTruncate,
    fileSync,
    fileSize,
    fileLock,
    fileUnlock,
    fileCheckReservedLock,
    fileControl,
    fileSectorSize,
    fileDeviceCharacteristics,
};

bool copyPath(char* destination, size_t capacity, const char* source) {
  if (destination == nullptr || source == nullptr || capacity == 0) return false;
  const size_t length = std::strlen(source);
  if (length >= capacity) return false;
  std::memcpy(destination, source, length + 1);
  return true;
}

int vfsOpen(sqlite3_vfs*, const char* name, sqlite3_file* raw, int flags, int* outFlags) {
  if (raw == nullptr) return SQLITE_CANTOPEN;
  PlayerSqliteFile* file = new (raw) PlayerSqliteFile();

  char generated[kPathCapacity]{};
  if (name == nullptr) {
    uint32_t random = 0;
    esp_fill_random(&random, sizeof(random));
    std::snprintf(generated, sizeof(generated), "/.crosspoint/.sqlite-%08lx.tmp",
                  static_cast<unsigned long>(random));
    name = generated;
    flags |= SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_DELETEONCLOSE;
  }

  if (!copyPath(file->path, sizeof(file->path), name)) {
    file->~PlayerSqliteFile();
    return SQLITE_CANTOPEN;
  }

  oflag_t openFlags = (flags & SQLITE_OPEN_READWRITE) != 0 ? O_RDWR : O_RDONLY;
  if ((flags & SQLITE_OPEN_CREATE) != 0) openFlags |= O_CREAT;
  if ((flags & SQLITE_OPEN_EXCLUSIVE) != 0) openFlags |= O_EXCL;

  file->file = Storage.open(file->path, openFlags);
  if (!file->file) {
    file->~PlayerSqliteFile();
    return SQLITE_CANTOPEN;
  }

  file->deleteOnClose = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
  file->base.pMethods = &kIoMethods;
  if (outFlags != nullptr) *outFlags = flags;
  return SQLITE_OK;
}

int vfsDelete(sqlite3_vfs*, const char* path, int) {
  if (path == nullptr) return SQLITE_IOERR_DELETE;
  if (!Storage.exists(path)) return SQLITE_OK;
  return Storage.remove(path) ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

int vfsAccess(sqlite3_vfs*, const char* path, int, int* result) {
  if (path == nullptr || result == nullptr) return SQLITE_IOERR_ACCESS;
  *result = Storage.exists(path) ? 1 : 0;
  return SQLITE_OK;
}

int vfsFullPathname(sqlite3_vfs*, const char* path, int outputSize, char* output) {
  if (path == nullptr || output == nullptr || outputSize <= 0) return SQLITE_CANTOPEN;
  const size_t length = std::strlen(path);
  if (length >= static_cast<size_t>(outputSize)) return SQLITE_CANTOPEN;
  std::memcpy(output, path, length + 1);
  return SQLITE_OK;
}

void* vfsDlOpen(sqlite3_vfs*, const char*) { return nullptr; }
void vfsDlError(sqlite3_vfs*, int size, char* error) {
  if (error != nullptr && size > 0) sqlite3_snprintf(size, error, "extensions unavailable");
}
void (*vfsDlSym(sqlite3_vfs*, void*, const char*))(void) { return nullptr; }
void vfsDlClose(sqlite3_vfs*, void*) {}

int vfsRandomness(sqlite3_vfs*, int bytes, char* output) {
  if (bytes <= 0 || output == nullptr) return 0;
  esp_fill_random(output, static_cast<size_t>(bytes));
  return bytes;
}

int vfsSleep(sqlite3_vfs*, int microseconds) {
  if (microseconds <= 0) return 0;
  const int milliseconds = microseconds / 1000;
  const int remainder = microseconds % 1000;
  if (milliseconds > 0) delay(static_cast<unsigned long>(milliseconds));
  if (remainder > 0) delayMicroseconds(static_cast<unsigned int>(remainder));
  return microseconds;
}

int vfsCurrentTime(sqlite3_vfs*, double* julianDay) {
  if (julianDay == nullptr) return SQLITE_ERROR;
  const std::time_t now = std::time(nullptr);
  *julianDay = 2440587.5 + static_cast<double>(now) / 86400.0;
  return SQLITE_OK;
}

int vfsGetLastError(sqlite3_vfs*, int, char*) { return 0; }

sqlite3_vfs kPlayerVfs = {
    1,
    static_cast<int>(sizeof(PlayerSqliteFile)),
    static_cast<int>(kPathCapacity - 1),
    nullptr,
    kVfsName,
    nullptr,
    vfsOpen,
    vfsDelete,
    vfsAccess,
    vfsFullPathname,
    vfsDlOpen,
    vfsDlError,
    vfsDlSym,
    vfsDlClose,
    vfsRandomness,
    vfsSleep,
    vfsCurrentTime,
    vfsGetLastError,
};

}  // namespace

bool registerPlayerSqliteVfs() {
  if (sqlite3_initialize() != SQLITE_OK) return false;
  return sqlite3_vfs_register(&kPlayerVfs, 1) == SQLITE_OK;
}

}  // namespace player

#endif
