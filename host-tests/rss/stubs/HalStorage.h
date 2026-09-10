#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace teststorage {
inline std::map<std::string, std::vector<unsigned char>> files;
inline bool writable = true;
inline bool renameFails = false;
inline size_t writeLimit = 1000000;
}  // namespace teststorage
class HalFile {
  std::vector<unsigned char>* bytes = nullptr;
  size_t position = 0;

 public:
  explicit operator bool() const { return bytes != nullptr; }
  void open(std::vector<unsigned char>& data) {
    bytes = &data;
    position = 0;
  }
  size_t write(const void* source, size_t size) {
    if (!bytes || !teststorage::writable) return 0;
    size = std::min(size, teststorage::writeLimit);
    const auto* data = static_cast<const unsigned char*>(source);
    bytes->insert(bytes->end(), data, data + size);
    return size;
  }
  int read(void* target, size_t size) {
    if (!bytes) return 0;
    const size_t count = std::min(size, bytes->size() - position);
    std::memcpy(target, bytes->data() + position, count);
    position += count;
    return static_cast<int>(count);
  }
  void flush() {}
  void close() { bytes = nullptr; }
};
struct TestStorage {
  bool ensureDirectoryExists(const char*) { return teststorage::writable; }
  bool exists(const char* path) { return teststorage::files.count(path); }
  bool remove(const char* path) { return teststorage::files.erase(path); }
  bool rename(const char* from, const char* to) {
    if (!teststorage::writable || teststorage::renameFails || !exists(from)) return false;
    teststorage::files[to] = std::move(teststorage::files[from]);
    teststorage::files.erase(from);
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    auto it = teststorage::files.find(path);
    if (it == teststorage::files.end()) return false;
    file.open(it->second);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    if (!teststorage::writable) return false;
    auto& data = teststorage::files[path];
    data.clear();
    file.open(data);
    return true;
  }
};
inline TestStorage Storage;
