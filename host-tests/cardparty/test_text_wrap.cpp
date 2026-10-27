#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "CardPartySdPack.h"
#include "CardPartyTextWrap.h"

static int measure(void*, const char* text) {
  int width = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    if ((*p & 0xc0) != 0x80) width += *p == 'W' ? 3 : 1;
  return width;
}
static std::string compact(std::string text) {
  std::string out;
  for (char c : text) if (c != ' ') out += c;
  return out;
}
int main() {
  for (const auto& text : {std::string(200, 'W') + "END", std::string("Grüße aus Köln, Öl und Bürokratie"),
                           std::string(150, 'a') + " äöüß END", std::string("one  two   three")}) {
    for (int width : {5, 17, 42}) {
      const char* cursor = text.c_str();
      std::string drawn;
      char line[160] = {};
      int rows = 0;
      while (cardparty::nextTextLine(cursor, line, sizeof(line), width, measure, nullptr)) {
        assert(++rows < 300);
        assert(measure(nullptr, line) <= width);
        assert(cardparty::sdpack::validText(reinterpret_cast<const uint8_t*>(line), std::strlen(line)));
        drawn += line;
      }
      assert(!*cursor && compact(drawn) == compact(text));
    }
  }
  char shortLine[8] = {};
  const char* cursor = "äöüßäöüß";
  while (cardparty::nextTextLine(cursor, shortLine, sizeof(shortLine), 5, measure, nullptr))
    assert(cardparty::sdpack::validText(reinterpret_cast<const uint8_t*>(shortLine), std::strlen(shortLine)));
  std::strcpy(shortLine, "äöü");
  cardparty::markTextTruncated(shortLine, sizeof(shortLine), 5, measure, nullptr);
  assert(std::strcmp(shortLine, "äö...") == 0);
  assert(measure(nullptr, shortLine) <= 5);
  std::puts("cardparty text wrap: long words, UTF-8 boundaries, byte/width limits and visible truncation OK");
}
