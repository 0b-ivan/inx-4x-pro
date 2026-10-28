#pragma once
// Pack list from the public GitHub release, one line per pack:
//   file.dat|Title|bytes
// Blank lines and lines starting with '#' are skipped. Any other malformed
// line rejects the whole list, so the network can never name a path, a URL
// or an oversized file. No Arduino dependency: host-testable.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CardPartySdPack.h"

namespace cardparty {
namespace catalog {
constexpr uint8_t kMaxEntries = 8;
constexpr size_t kMaxBytes = 1024;
constexpr size_t kNameBytes = 48;

struct Entry {
  char file[kNameBytes] = {};
  char title[kNameBytes] = {};
  uint32_t bytes = 0;
};

// A plain file name the packs folder can hold: letters, digits, '.', '_',
// '-', no leading '.', ending in ".dat".
inline bool validFileName(const char* name, size_t len) {
  if (len < 5 || len >= kNameBytes || name[0] == '.') return false;
  for (size_t i = 0; i < len; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-';
    if (!ok) return false;
  }
  return std::memcmp(name + len - 4, ".dat", 4) == 0;
}

class Catalog final {
 public:
  void clear() { count_ = 0; }
  uint8_t count() const { return count_; }
  const Entry& at(uint8_t i) const { return entries_[i < count_ ? i : 0]; }

  // Replaces the list only when every line is valid and at least one pack
  // is listed.
  bool parse(const char* text, size_t len) {
    if (len > kMaxBytes) return false;
    Entry parsed[kMaxEntries];
    uint8_t n = 0;
    size_t at = 0;
    while (at < len) {
      size_t end = at;
      while (end < len && text[end] != '\n') ++end;
      size_t lineEnd = end;
      if (lineEnd > at && text[lineEnd - 1] == '\r') --lineEnd;
      if (lineEnd > at && text[at] != '#') {
        if (n == kMaxEntries || !parseLine(text + at, lineEnd - at, parsed[n])) return false;
        for (uint8_t i = 0; i < n; ++i)
          if (std::strcmp(parsed[i].file, parsed[n].file) == 0) return false;
        ++n;
      }
      at = end + 1;
    }
    if (n == 0) return false;
    for (uint8_t i = 0; i < n; ++i) entries_[i] = parsed[i];
    count_ = n;
    return true;
  }

 private:
  static bool parseLine(const char* line, size_t len, Entry& out) {
    const char* bar1 = static_cast<const char*>(std::memchr(line, '|', len));
    if (!bar1) return false;
    const size_t fileLen = static_cast<size_t>(bar1 - line);
    const char* rest = bar1 + 1;
    const size_t restLen = len - fileLen - 1;
    const char* bar2 = static_cast<const char*>(std::memchr(rest, '|', restLen));
    if (!bar2) return false;
    const size_t titleLen = static_cast<size_t>(bar2 - rest);
    const char* digits = bar2 + 1;
    const size_t digitsLen = restLen - titleLen - 1;
    if (!validFileName(line, fileLen)) return false;
    if (titleLen == 0 || titleLen >= kNameBytes ||
        !sdpack::validText(reinterpret_cast<const uint8_t*>(rest), titleLen))
      return false;
    if (digitsLen == 0 || digitsLen > 7) return false;
    uint32_t bytes = 0;
    for (size_t i = 0; i < digitsLen; ++i) {
      if (digits[i] < '0' || digits[i] > '9') return false;
      bytes = bytes * 10 + static_cast<uint32_t>(digits[i] - '0');
    }
    if (bytes <= sdpack::kHeaderBytes || bytes > sdpack::kMaxPackBytes) return false;
    out = Entry{};
    std::memcpy(out.file, line, fileLen);
    std::memcpy(out.title, rest, titleLen);
    out.bytes = bytes;
    return true;
  }

  Entry entries_[kMaxEntries];
  uint8_t count_ = 0;
};
}  // namespace catalog
}  // namespace cardparty
