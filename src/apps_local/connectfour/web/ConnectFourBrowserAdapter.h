#pragma once
// Authoritative Connect Four browser adapter; freestanding C++17, no Arduino.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "../ConnectFourCore.h"
#include "../../web/SeatSession.h"

namespace c4web {
enum class Kind : uint8_t { Join, Drop, Rematch, Resume };
struct Command {
  Kind kind = Kind::Join;
  char token[33] = {};
  char secret[33] = {};
  uint32_t revision = 0;
  uint8_t column = 0;
};
namespace detail {
struct Reader {
  const uint8_t* data;
  size_t size, pos = 0;
  void skip() {
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' ||
                          data[pos] == '\r' || data[pos] == '\t')) ++pos;
  }
  bool match(char ch) {
    skip();
    if (pos >= size || data[pos] != static_cast<uint8_t>(ch)) return false;
    ++pos; return true;
  }
  bool str(char* out, size_t capacity) {
    if (!match('"')) return false;
    size_t at = 0;
    while (pos < size && data[pos] != '"') {
      const uint8_t c = data[pos++];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') || at + 1 >= capacity) return false;
      out[at++] = static_cast<char>(c);
    }
    if (!at || pos >= size) return false;
    ++pos; out[at] = '\0'; return true;
  }
  bool number(uint32_t& out) {
    skip();
    if (pos >= size || data[pos] < '0' || data[pos] > '9') return false;
    if (data[pos] == '0' && pos + 1 < size && data[pos+1] >= '0' && data[pos+1] <= '9') return false;
    uint32_t value = 0;
    while (pos < size && data[pos] >= '0' && data[pos] <= '9') {
      const uint32_t digit = data[pos++] - '0';
      if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10u) return false;
      value = value * 10u + digit;
    }
    out = value; return true;
  }
};
inline bool hex32(const char* value) {
  if (std::strlen(value) != 32) return false;
  for (int i = 0; i < 32; ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}
}
inline bool parseCommand(const uint8_t* data, size_t length, Command& out) {
  if (!data || !length || length > 128) return false;
  detail::Reader reader{data, length};
  Command next; char action[12]{};
  if (!reader.match('[') || !reader.str(action, sizeof(action)) ||
      !reader.match(',') || !reader.str(next.token, sizeof(next.token)) ||
      !detail::hex32(next.token) || !reader.match(',') ||
      !reader.number(next.revision)) return false;
  if (!std::strcmp(action, "join")) next.kind = Kind::Join;
  else if (!std::strcmp(action, "drop")) {
    uint32_t col = 0;
    if (!reader.match(',') || !reader.number(col) || col >= connectfour::kColumns) return false;
    next.kind = Kind::Drop; next.column = static_cast<uint8_t>(col);
  } else if (!std::strcmp(action, "rematch")) next.kind = Kind::Rematch;
  else if (!std::strcmp(action, "resume")) {
    if (!reader.match(',') || !reader.str(next.secret, sizeof(next.secret)) ||
        !detail::hex32(next.secret)) return false;
    next.kind = Kind::Resume;
  } else return false;
  if (!reader.match(']')) return false;
  reader.skip();
  if (reader.pos != length) return false;
  out = next; return true;
}
class Round final {
 public:
  using RandomWord = webplay::SeatSession::RandomWord;
  void begin(RandomWord entropy) {
    connectfour::start(game_); joined_ = false; revision_ = 0;
    seat_.begin(entropy);
  }
  void stop() { seat_.stop(); joined_ = false; }
  bool joined() const { return joined_; }
  bool hasOwner() const { return seat_.hasOwner(); }
  uint8_t owner() const { return seat_.owner(); }
  uint32_t revision() const { return revision_; }
  const char* token() const { return seat_.token(); }
  const char* resumeToken() const { return seat_.resumeToken(); }
  const connectfour::Game& game() const { return game_; }
  bool authorized(const char* token, const char* secret) const {
    return seat_.authorized(token, secret);
  }
  bool receive(uint8_t client, const uint8_t* bytes, size_t length, uint32_t now,
               RandomWord entropy) {
    Command cmd;
    if (!parseCommand(bytes, length, cmd)) return false;
    if (cmd.kind == Kind::Resume) {
      if (!joined_ || !seat_.admit(client, cmd.token, false, true, cmd.secret, now)) return false;
      if (!seat_.hasOwner()) seat_.claim(client, false, entropy);
      return true; // Read-only resync permits a stale browser revision.
    }
    if (cmd.revision != revision_ || revision_ == std::numeric_limits<uint32_t>::max()) return false;
    if (cmd.kind == Kind::Join) {
      if (joined_ || !seat_.admit(client, cmd.token, true, false, "", now)) return false;
      joined_ = true; seat_.claim(client, true, entropy); return true;
    }
    if (!joined_ || !seat_.hasOwner() || seat_.owner() != client) return false;
    if (cmd.kind == Kind::Drop) {
      if (game_.turn != connectfour::kDark || !connectfour::canDrop(game_, cmd.column) ||
          !seat_.admit(client, cmd.token, false, false, "", now)) return false;
      if (!connectfour::drop(game_, cmd.column)) return false;
      ++revision_; return true;
    }
    if (cmd.kind == Kind::Rematch) {
      if (!connectfour::over(game_) || !seat_.admit(client, cmd.token, false, false, "", now)) return false;
      connectfour::start(game_); ++revision_; return true;
    }
    return false;
  }
  bool deviceDrop(uint8_t column) {
    if (!joined_ || revision_ == std::numeric_limits<uint32_t>::max() ||
        game_.turn != connectfour::kLight || !connectfour::drop(game_, column)) return false;
    ++revision_; return true;
  }
  bool deviceRematch() {
    if (!joined_ || revision_ == std::numeric_limits<uint32_t>::max() || !connectfour::over(game_)) return false;
    connectfour::start(game_);
    ++revision_;
    return true;
  }
  bool disconnected(uint8_t client, RandomWord entropy) {
    return seat_.disconnect(client, entropy);
  }
  // Public snapshot: 42 chars, column-major and bottom-first (0/1/2).
  // Private owner and resume capabilities never enter this frame.
  size_t snapshot(char* out, size_t capacity) const {
    if (!out || !capacity) return 0;
    char board[connectfour::kCells+1];
    size_t i = 0;
    for (int c = 0; c < connectfour::kColumns; ++c)
      for (int r = 0; r < connectfour::kRows; ++r)
        board[i++] = static_cast<char>('0' + game_.cell[c][r]);
    board[i] = '\0';
    const int n = std::snprintf(out, capacity,
      "{\"type\":\"c4\",\"phase\":\"%s\",\"r\":%lu,\"turn\":%u,\"outcome\":%u,\"last\":[%d,%d],\"board\":\"%s\"}",
      !joined_ ? "waiting" : (connectfour::over(game_) ? "finished" : "playing"),
      static_cast<unsigned long>(revision_), static_cast<unsigned>(game_.turn),
      static_cast<unsigned>(game_.outcome), static_cast<int>(game_.lastColumn),
      static_cast<int>(game_.lastRow), board);
    if (n < 0 || static_cast<size_t>(n) >= capacity) { out[0] = '\0'; return 0; }
    return static_cast<size_t>(n);
  }
 private:
  webplay::SeatSession seat_;
  connectfour::Game game_{};
  uint32_t revision_ = 0;
  bool joined_ = false;
};
} // namespace c4web
