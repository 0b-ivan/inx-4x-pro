#include "BrowserCommands.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace bshipweb {
namespace {
class Reader {
 public:
  Reader(const uint8_t* bytes, size_t size) : at(bytes), end(bytes + size) {}
  void space() {
    while (at != end && (*at == ' ' || *at == '\n' || *at == '\r' || *at == '\t')) ++at;
  }
  bool take(char c) {
    space();
    if (at == end || *at != c) return false;
    ++at;
    return true;
  }
  bool literal(const char* s) {
    space();
    const size_t n = strlen(s);
    if (static_cast<size_t>(end - at) < n || memcmp(at, s, n)) return false;
    at += n;
    return true;
  }
  bool number(uint32_t& n) {
    space();
    n = 0;
    if (at == end || *at < '0' || *at > '9') return false;
    if (*at == '0') {
      ++at;
      return true;
    }
    while (at != end && *at >= '0' && *at <= '9') {
      const uint32_t digit = *at++ - '0';
      if (n > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
      n = n * 10 + digit;
    }
    return true;
  }
  bool token(char* out) {
    if (!take('"')) return false;
    for (int i = 0; i < 32; ++i) {
      if (at == end || !((*at >= '0' && *at <= '9') || (*at >= 'a' && *at <= 'f'))) return false;
      out[i] = *at++;
    }
    out[32] = 0;
    if (at == end || *at != '"') return false;
    ++at;
    return true;
  }
  bool done() {
    space();
    return at == end;
  }

 private:
  const uint8_t* at;
  const uint8_t* end;
};
}  // namespace

bool parseCommand(const uint8_t* bytes, size_t size, Command& out) {
  if (!bytes || !size || size > kMaxCommandBytes) return false;
  Reader r(bytes, size);
  Command candidate;
  if (!r.take('[')) return false;
  if (r.literal("\"profile\""))
    candidate.kind = CommandKind::Profile;
  else if (r.literal("\"place\""))
    candidate.kind = CommandKind::Place;
  else if (r.literal("\"ready\""))
    candidate.kind = CommandKind::Ready;
  else if (r.literal("\"fire\""))
    candidate.kind = CommandKind::Fire;
  else if (r.literal("\"rematch\""))
    candidate.kind = CommandKind::Rematch;
  else if (r.literal("\"resume\""))
    candidate.kind = CommandKind::Resume;
  else
    return false;

  if (!r.take(',') || !r.token(candidate.token) || !r.take(',') || !r.number(candidate.revision)) return false;

  if (candidate.kind == CommandKind::Profile || candidate.kind == CommandKind::Place) {
    for (int i = 0; i < 3; ++i) {
      uint32_t value;
      if (!r.take(',') || !r.number(value)) return false;
      const uint32_t limit = candidate.kind == CommandKind::Profile ? 13 : (i == 0 ? 4 : i == 1 ? 99 : 1);
      if (value > limit) return false;
      candidate.value[i] = static_cast<uint8_t>(value);
    }
  } else if (candidate.kind == CommandKind::Fire) {
    uint32_t cell;
    if (!r.take(',') || !r.number(cell) || cell > 99) return false;
    candidate.value[0] = static_cast<uint8_t>(cell);
  } else if (candidate.kind == CommandKind::Resume) {
    if (!r.take(',') || !r.token(candidate.resumeToken)) return false;
  }

  if (!r.take(']') || !r.done()) return false;
  out = candidate;
  return true;
}

size_t serializePlacement(const PlacementView& v, bool accepted, char* out, size_t capacity) {
  if (!out || !capacity) return 0;
  const int n =
      snprintf(out, capacity,
               "{\"type\":\"placement\",\"accepted\":%s,\"revision\":%lu,\"profile\":%s,\"ready\":%s,"
               "\"slots\":[%u,%u,%u],\"ships\":[[%u,%u],[%u,%u],[%u,%u],[%u,%u],[%u,%u]]}",
               accepted ? "true" : "false", static_cast<unsigned long>(v.revision), v.profile ? "true" : "false",
               v.ready ? "true" : "false", v.slots[0], v.slots[1], v.slots[2], v.bow[0], v.horizontal[0], v.bow[1],
               v.horizontal[1], v.bow[2], v.horizontal[2], v.bow[3], v.horizontal[3], v.bow[4], v.horizontal[4]);
  if (n < 0 || static_cast<size_t>(n) >= capacity) {
    out[0] = 0;
    return 0;
  }
  return static_cast<size_t>(n);
}
}  // namespace bshipweb
