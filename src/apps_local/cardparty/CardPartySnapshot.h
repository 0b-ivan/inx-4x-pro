#pragma once
// Crash-safe SD snapshot: write <path>.part, keep the previous file as
// <path>.bak, then rename. Power can fail between any two steps; load()
// then still finds the old or the new snapshot, never neither.
//
// Store provides: exists(p), remove(p), rename(from, to),
// write(p, bytes, size) and read(p, std::vector<uint8_t>&, maxBytes), each
// returning false on failure. No SD code in here, so host tests drive it.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cardparty {
namespace snapshot {

struct Files {
  const char* path;
  const char* part;
  const char* backup;
};

enum class Loaded : uint8_t { None, Primary, Backup };

// decode(bytes, size) must leave its target untouched when it returns false.
template <class Store, class Decode>
Loaded load(Store& store, const Files& files, size_t maxBytes, Decode decode) {
  const auto read = [&](const char* path) {
    std::vector<uint8_t> bytes;
    return store.exists(path) && store.read(path, bytes, maxBytes) && decode(bytes.data(), bytes.size());
  };
  if (read(files.path)) return Loaded::Primary;
  if (!read(files.backup)) return Loaded::None;
  // Promote the good backup, so the next save does not rotate the corrupt
  // primary over the only recoverable copy.
  store.remove(files.path);
  store.rename(files.backup, files.path);
  return Loaded::Backup;
}

template <class Store>
bool save(Store& store, const Files& files, const uint8_t* bytes, size_t size) {
  store.remove(files.part);
  if (!store.write(files.part, bytes, size)) {
    store.remove(files.part);
    return false;
  }
  store.remove(files.backup);
  const bool hadOld = store.exists(files.path);
  if (hadOld && !store.rename(files.path, files.backup)) {
    store.remove(files.part);
    return false;
  }
  if (!store.rename(files.part, files.path)) {
    if (hadOld) store.rename(files.backup, files.path);
    store.remove(files.part);
    return false;
  }
  return true;
}

}  // namespace snapshot
}  // namespace cardparty
