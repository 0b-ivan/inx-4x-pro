#pragma once
#include <cstddef>
#include <cstring>

namespace cardparty {
using TextMeasure = int (*)(void*, const char*);

// Input is validated UTF-8. Long words continue on the next line.
inline bool nextTextLine(const char*& text, char* line, size_t capacity, int width,
                         TextMeasure measure, void* context) {
  if (!text || !line || capacity < 5 || width <= 0 || !measure) return false;
  while (*text == ' ') ++text;
  if (!*text) return false;
  const char* p = text;
  const char* afterSpace = nullptr;
  size_t used = 0, space = 0;
  line[0] = 0;
  while (*p) {
    size_t bytes = 1;
    while (p[bytes] && (static_cast<unsigned char>(p[bytes]) & 0xc0) == 0x80) ++bytes;
    if (used + bytes >= capacity) break;
    std::memcpy(line + used, p, bytes);
    line[used + bytes] = 0;
    if (measure(context, line) > width) {
      line[used] = 0;
      if (!used) return false;
      break;
    }
    used += bytes;
    p += bytes;
    if (bytes == 1 && p[-1] == ' ') { space = used - 1; afterSpace = p; }
  }
  if (*p && afterSpace) { used = space; p = afterSpace; }
  while (used && line[used - 1] == ' ') --used;
  line[used] = 0;
  while (*p == ' ') ++p;
  text = p;
  return used != 0;
}

inline void markTextTruncated(char* line, size_t capacity, int width, TextMeasure measure, void* context) {
  size_t used = std::strlen(line);
  for (;;) {
    if (used + 4 <= capacity) {
      std::memcpy(line + used, "...", 4);
      if (measure(context, line) <= width) return;
    }
    if (!used) { line[0] = 0; return; }
    do { --used; } while (used && (static_cast<unsigned char>(line[used]) & 0xc0) == 0x80);
    line[used] = 0;
  }
}
}  // namespace cardparty
