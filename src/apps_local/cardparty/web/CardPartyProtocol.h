#pragma once
// Bounded, canonical browser-command wire format. Freestanding C++17.
// Only these exact JSON message shapes are accepted; the browser emits them
// in this order. No generic object parser, type coercion, unknown fields,
// duplicate keys, trailing bytes, escaped control characters or 0x NULs.
//
// {"type":"join","name":"Ivan"}
// {"type":"resume","token":"32 lowercase hex chars"}
// {"type":"submit","token":"...","revision":0,"slots":[0,1,2]}
// {"type":"judge","token":"...","revision":0,"selected":0}
// {"type":"pause","token":"..."}    also "unpause" and "again"
//
// This is a deliberately narrow application protocol, not a JSON library.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../CardPartySessions.h"

namespace cardparty {
namespace web {

constexpr size_t kMaxCommand = 192;
enum class Kind : uint8_t { Join, Resume, Submit, Judge, Pause, Unpause, Again, Strike, Fame };
struct Command {
  Kind kind = Kind::Join;
  char name[24] = {};
  uint64_t playerId = 0;  // optional stable, non-secret browser identity
  uint8_t secret[kTicketBytes] = {};
  uint32_t revision = 0;
  uint8_t slots[kPickMax] = {};
  uint8_t count = 0;
  uint8_t selected = 0;
  uint8_t slot = 0, page = 0;
};

class Reader final {
 public:
  Reader(const uint8_t* data, size_t length) : data_(data), length_(length) {}
  bool token(const char* literal) {
    const size_t before = position_;
    ws();
    while (*literal) {
      if (position_ >= length_ || data_[position_] != static_cast<uint8_t>(*literal)) {
        position_ = before;  // optional delimiters must not consume ']'
        return false;
      }
      ++position_;
      ++literal;
    }
    return true;
  }
  bool str(char* target, size_t capacity, bool allowEscapes) {
    ws();
    if (!target || capacity < 2 || position_ >= length_ || data_[position_++] != '"') return false;
    size_t n = 0;
    while (position_ < length_) {
      uint8_t c = data_[position_++];
      if (c == '"') {
        target[n] = 0;
        return n > 0;
      }
      if (c < 0x20 || c == 0x7f) return false;
      if (c == '\\') {
        if (!allowEscapes || position_ >= length_) return false;
        c = data_[position_++];
        if (c != '"' && c != '\\' && c != '/') return false;
      }
      if (n + 1 >= capacity) return false;
      target[n++] = static_cast<char>(c);
    }
    return false;
  }
  bool number(uint32_t& out, uint32_t max) {
    ws();
    if (position_ >= length_ || data_[position_] < '0' || data_[position_] > '9') return false;
    uint32_t result = 0;
    if (data_[position_] == '0' && position_ + 1 < length_ && data_[position_ + 1] >= '0' &&
        data_[position_ + 1] <= '9')
      return false;
    while (position_ < length_ && data_[position_] >= '0' && data_[position_] <= '9') {
      const uint32_t digit = data_[position_++] - '0';
      if (digit > max || result > (max - digit) / 10u) return false;
      result = result * 10u + digit;
    }
    out = result;
    return true;
  }
  bool finish() {
    ws();
    return position_ == length_;
  }

 private:
  void ws() {
    while (position_ < length_) {
      const uint8_t c = data_[position_];
      if (c != ' ' && c != '\n' && c != '\t' && c != '\r') break;
      ++position_;
    }
  }
  const uint8_t* data_ = nullptr;
  size_t length_ = 0, position_ = 0;
};

inline int nibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}
inline void encodeSecret(const uint8_t secret[kTicketBytes], char out[33]) {
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < kTicketBytes; ++i) {
    out[i * 2] = hex[secret[i] >> 4];
    out[i * 2 + 1] = hex[secret[i] & 15];
  }
  out[32] = 0;
}
inline bool decodeSecret(const char* token, uint8_t out[kTicketBytes]) {
  if (!token || std::strlen(token) != 32) return false;
  uint8_t any = 0;
  for (size_t i = 0; i < kTicketBytes; ++i) {
    const int a = nibble(token[i * 2]), b = nibble(token[i * 2 + 1]);
    if (a < 0 || b < 0) return false;
    out[i] = static_cast<uint8_t>((a << 4) | b);
    any |= out[i];
  }
  return any != 0;
}
inline bool decodePlayerId(const char* hex, uint64_t& id) {
  if (!hex || std::strlen(hex) != 16) return false;
  uint64_t value = 0;
  for (size_t i = 0; i < 16; ++i) {
    const int n = nibble(hex[i]);
    if (n < 0) return false;
    value = (value << 4) | static_cast<uint64_t>(n);
  }
  if (!value) return false;
  id = value;
  return true;
}
inline void encodePlayerId(uint64_t id, char out[17]) {
  static constexpr char hex[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i) {
    out[i] = hex[id & 15u];
    id >>= 4;
  }
  out[16] = 0;
}

inline bool parse(const uint8_t* data, size_t length, Command& out) {
  if (!data || length == 0 || length > kMaxCommand) return false;
  Reader r(data, length);
  char kind[12] = {};
  Command command{};
  if (!r.token("{") || !r.token("\"type\":") || !r.str(kind, sizeof(kind), false)) return false;
  if (std::strcmp(kind, "join") == 0) {
    command.kind = Kind::Join;
    if (!r.token(",\"name\":") || !r.str(command.name, sizeof(command.name), true)) return false;
    // No unsafe controls or multiline input, and prevent HTML/markup confusion.
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(command.name); *p; ++p)
      if (*p < 0x20 || *p == 0x7f || *p == '<' || *p == '>') return false;
    // Canonical optional identity, distinct from the 128-bit seat ticket.
    // The first join omits it; the X4 issues one in the welcome frame.
    if (r.token(",\"pid\":")) {
      char pid[17] = {};
      if (!r.str(pid, sizeof(pid), false) || !decodePlayerId(pid, command.playerId)) return false;
    }
  } else {
    // Every other command proves its seat with the ticket alone.
    static constexpr struct {
      const char* name;
      Kind kind;
    } kTicketed[] = {{"resume", Kind::Resume}, {"submit", Kind::Submit},   {"judge", Kind::Judge},
                     {"pause", Kind::Pause},   {"unpause", Kind::Unpause}, {"again", Kind::Again},
                     {"strike", Kind::Strike}, {"fame", Kind::Fame}};
    bool known = false;
    for (const auto& entry : kTicketed)
      if (std::strcmp(kind, entry.name) == 0) {
        command.kind = entry.kind;
        known = true;
      }
    if (!known) return false;
    char token[33] = {};
    if (!r.token(",\"token\":") || !r.str(token, sizeof(token), false) || !decodeSecret(token, command.secret))
      return false;
    if (command.kind == Kind::Strike || command.kind == Kind::Fame) {
      uint32_t number = 0;
      if (command.kind == Kind::Strike) {
        if (!r.token(",\"slot\":") || !r.number(number,kHandMax-1)) return false;
        command.slot = static_cast<uint8_t>(number);
      } else {
        if (!r.token(",\"page\":") || !r.number(number,255)) return false;
        command.page = static_cast<uint8_t>(number);
      }
    }
    if (command.kind == Kind::Submit || command.kind == Kind::Judge) {
      if (!r.token(",\"revision\":") || !r.number(command.revision, std::numeric_limits<uint32_t>::max())) return false;
      if (command.kind == Kind::Submit) {
        if (!r.token(",\"slots\":[")) return false;
        uint32_t slot = 0;
        if (!r.number(slot, kHandMax - 1)) return false;
        command.slots[command.count++] = static_cast<uint8_t>(slot);
        while (command.count < kPickMax && r.token(",")) {
          if (!r.number(slot, kHandMax - 1)) return false;
          command.slots[command.count++] = static_cast<uint8_t>(slot);
        }
        if (!r.token("]")) return false;
      } else {
        uint32_t chosen = 0;
        if (!r.token(",\"selected\":") || !r.number(chosen, kPileMax - 1)) return false;
        command.selected = static_cast<uint8_t>(chosen);
      }
    }
  }
  if (!r.token("}") || !r.finish()) return false;
  out = command;
  return true;
}

// Escape arbitrary printable UTF-8 card/name content for JSON without allowing
// script injection. Raw control bytes are escaped to a canonical unicode form.
class JsonWriter final {
 public:
  JsonWriter(char* data, size_t size) : data_(data), size_(size) {
    if (size_ != 0 && data_) data_[0] = 0;
  }
  bool raw(const char* str) {
    if (!ok_ || !str) return false;
    for (const char* p = str; *p; ++p) put(*p);
    return ok_;
  }
  bool num(uint32_t value) {
    char digits[11] = {};
    size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value);
    while (n) put(digits[--n]);
    return ok_;
  }
  bool boolean(bool value) { return raw(value ? "true" : "false"); }
  bool quoted(const char* str) {
    put('"');
    escaped(str);
    put('"');
    return ok_;
  }
  // Append a fragment inside an already opened JSON string.
  bool escaped(const char* str) {
    if (!str) return ok_;
    const char hex[] = "0123456789abcdef";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
      if (*p == '"' || *p == '\\') {
        put('\\');
        put(static_cast<char>(*p));
      } else if (*p < 0x20) {
        raw("\\u00");
        put(hex[(*p >> 4) & 15]);
        put(hex[*p & 15]);
      } else
        put(static_cast<char>(*p));
    }
    return ok_;
  }
  size_t size() const { return ok_ ? used_ : 0; }
  bool ok() const { return ok_; }

 private:
  void put(char c) {
    if (!ok_ || !data_ || used_ + 1 >= size_) {
      ok_ = false;
      return;
    }
    data_[used_++] = c;
    data_[used_] = 0;
  }
  char* data_ = nullptr;
  size_t size_ = 0, used_ = 0;
  bool ok_ = true;
};

}  // namespace web
}  // namespace cardparty
