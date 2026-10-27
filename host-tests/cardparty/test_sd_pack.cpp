// Native regression suite for the SAME read-only pack loader as the X4.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "CardPartyCategories.h"
#include "CardPartyCore.h"
#include "CardPartySdPack.h"

using namespace cardparty;
using namespace cardparty::sdpack;

struct Buffer : ByteSource {
  std::vector<uint8_t> bytes;
  uint32_t failOffset = kMaxPackBytes + 1;
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
  bool read(uint32_t offset, void* dst, uint32_t n) override {
    if (offset >= failOffset) return false;
    if (offset > bytes.size() || n > bytes.size() - offset) return false;
    std::memcpy(dst, bytes.data() + offset, n);
    return true;
  }
};
static void u16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
static void u32(std::vector<uint8_t>& v, uint32_t x) {
  for (unsigned i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (i * 8)));
}
static void str(std::vector<uint8_t>& v, const std::string& s) {
  u16(v, static_cast<uint16_t>(s.size()));
  v.insert(v.end(), s.begin(), s.end());
  v.push_back(0);
}

// Categories: 0 Basis (on), 1 Böse (on), 2 FSK 18 (adult; the flag byte also
// claims "on" to prove the reader overrides it).
struct Layout {
  size_t firstPrompt = 0, secondPrompt = 0, firstAnswer = 0;
};
static Buffer fixture(bool released = true, Layout* layout = nullptr, uint8_t categories = 3) {
  Buffer out;
  std::vector<uint8_t> body;
  for (const char* meta : {"QA Deck", "CC0-1.0", "https://example.org/l", "Fixture", "https://example.org"})
    str(body, meta);
  const struct { uint8_t flags; const char* name; } cats[] = {
      {kCategoryDefaultOn, "Basis"}, {kCategoryDefaultOn, "Böse"}, {kCategoryDefaultOn | kCategoryAdult, "FSK 18"}};
  for (uint8_t i = 0; i < categories; ++i) {
    body.push_back(cats[i].flags);
    str(body, cats[i].name);
  }
  if (layout) layout->firstPrompt = 32 + body.size();
  const char first[] = "Ohne Lücke?";
  u16(body, static_cast<uint16_t>(std::strlen(first)));
  body.push_back(1);
  body.push_back(0);
  u16(body, 0);  // untagged: always plays
  body.insert(body.end(), first, first + std::strlen(first));
  body.push_back(0);
  if (layout) layout->secondPrompt = 32 + body.size();
  const char second[] = "____ ____";
  u16(body, static_cast<uint16_t>(std::strlen(second)));
  body.push_back(2);
  body.push_back(0);
  u16(body, categories >= 3 ? 0b110 : 0);  // Böse + FSK 18
  body.insert(body.end(), second, second + std::strlen(second));
  body.push_back(0);
  if (layout) layout->firstAnswer = 32 + body.size();
  // 100 answers: 60 untagged, 30 Böse, 10 FSK 18.
  for (int i = 0; i < 100; ++i) {
    const std::string text = "Eine Antwort " + std::to_string(i);
    u16(body, static_cast<uint16_t>(text.size()));
    u16(body, categories >= 3 ? (i < 60 ? 0 : i < 90 ? 0b010 : 0b100) : 0);
    body.insert(body.end(), text.begin(), text.end());
    body.push_back(0);
  }
  out.bytes = {'X', 'C', 'A', 'W', 'P', 'K', '2', 0};
  u16(out.bytes, 2);
  u16(out.bytes, released ? 1 : 0);
  u16(out.bytes, 2);
  u16(out.bytes, 100);
  u32(out.bytes, static_cast<uint32_t>(body.size()));
  u32(out.bytes, crc32(body.data(), body.size()));
  u32(out.bytes, 1);
  out.bytes.push_back(categories);
  out.bytes.insert(out.bytes.end(), 3, 0);
  out.bytes.insert(out.bytes.end(), body.begin(), body.end());
  return out;
}
static void refreshCrc(Buffer& data) {
  const uint32_t sum = crc32(data.bytes.data() + 32, data.bytes.size() - 32);
  for (int i = 0; i < 4; ++i) data.bytes[20 + i] = static_cast<uint8_t>(sum >> (i * 8));
}

static void categoriesAndFilter() {
  Layout at;
  Buffer input = fixture(true, &at);
  Pack pack;
  assert(pack.load(input));
  assert(std::strcmp(pack.title(), "QA Deck") == 0 && std::strcmp(pack.rightsHolder(), "Fixture") == 0);
  assert(std::strcmp(pack.license(), "CC0-1.0") == 0 && std::strcmp(pack.sourceUrl(), "https://example.org") == 0);
  assert(pack.categoryCount() == 3);
  assert(std::strcmp(pack.category(1).name, "Böse") == 0 && pack.category(1).defaultOn);
  // Adult is never on by default, whatever the flag byte says.
  assert(pack.category(2).adult && !pack.category(2).defaultOn);
  assert(pack.promptMask(1) == 0b110 && pack.answerMask(95) == 0b100);

  const uint16_t defaults = categories::defaults(pack);
  assert(defaults == 0b011);
  categories::FilteredDeck filtered;
  assert(filtered.build(pack, defaults));
  // FSK 18 off: the Böse+FSK 18 prompt and the 10 adult answers are out.
  assert(filtered.deck().promptsCount == 1 && filtered.deck().answersCount == 90);
  for (uint16_t i = 0; i < filtered.deck().answersCount; ++i)
    assert(std::strcmp(filtered.deck().answers[i].text, "Eine Antwort 90") != 0);
  assert(filtered.build(pack, 0b111));
  assert(filtered.deck().promptsCount == 2 && filtered.deck().answersCount == 100);
  assert(filtered.build(pack, 0));  // only untagged cards
  assert(filtered.deck().promptsCount == 1 && filtered.deck().answersCount == 60);
  assert(categories::cardsIn(pack, 1, true) == 30 && categories::cardsIn(pack, 2, false) == 1);

  // A full table at hand 5 needs 8*5 + 7*maxDraw + 3 distinct answers.
  assert(categories::requiredAnswers(filtered.deck(), 5) == 43);
  assert(categories::playable(filtered.deck(), 5));
  Deck tiny = filtered.deck();
  tiny.answersCount = 42;
  assert(!categories::playable(tiny, 5));
  assert(filtered.build(pack, 0b011));
  Game game;
  assert(game.configure(filtered.deck(), Rules{5, 10, true}, 7));
  assert(game.join("A") == 0 && game.join("B") == 1 && game.join("C") == 2);
  assert(game.start());

  // Saved selection: keyed to the pack CRC, bounded to its categories.
  uint8_t saved[categories::kSelectionBytes] = {};
  categories::encode(pack.crc(), 0b101, saved);
  uint16_t mask = 0;
  assert(categories::decode(saved, pack.crc(), 3, mask) && mask == 0b101);
  assert(!categories::decode(saved, pack.crc() ^ 1, 3, mask));
  categories::encode(pack.crc(), 0b1000, saved);
  assert(!categories::decode(saved, pack.crc(), 3, mask));
  saved[0] = 'X';
  assert(!categories::decode(saved, pack.crc(), 3, mask));

  // Masks naming categories the pack does not declare are corrupt.
  Buffer bad = input;
  bad.bytes[at.firstPrompt + 4] = 0b1000;
  refreshCrc(bad);
  assert(!pack.load(bad));
  bad = input;
  bad.bytes[at.firstAnswer + 2] = 0b1000;
  refreshCrc(bad);
  assert(!pack.load(bad));
  // Unknown category flag bits, too many categories, header padding.
  bad = fixture(true, nullptr, 3);
  bad.bytes[28] = 17;
  assert(!pack.load(bad));
  bad = fixture(true, nullptr, 3);
  bad.bytes[29] = 1;
  assert(!pack.load(bad));
  // A pack without categories is valid and plays everything.
  Buffer plain = fixture(true, nullptr, 0);
  assert(pack.load(plain) && pack.categoryCount() == 0 && categories::defaults(pack) == 0);
  assert(filtered.build(pack, 0) && filtered.deck().answersCount == 100);
  std::puts("cardparty sd pack: categories, adult off by default, filter, min answers, saved selection OK");
}

int main() {
  Layout at;
  const auto good = fixture(true, &at);
  Pack pack;
  Buffer input = good;
  assert(pack.load(input) && pack.ready());
  assert(pack.deck().promptsCount == 2 && pack.deck().answersCount == 100);
  assert(std::strcmp(pack.deck().prompts[0].text, "Ohne Lücke?") == 0);
  assert(pack.deck().prompts[1].pick == 2);
  Game game;
  assert(game.configure(pack.deck(), Rules{5, 10, true}, 1));
  assert(game.join("A") == 0 && game.join("B") == 1 && game.join("C") == 2);
  assert(game.start());  // zero-placeholder prompt participates in a real game
  Buffer bad = good;
  bad.bytes[0] = '?';
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[6] = '1';  // format 1 magic is no longer accepted
  assert(!pack.load(bad));
  bad = good;
  bad.bytes.back() ^= 0x20;
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[10] = 2;
  assert(!pack.load(bad));
  bad = good;
  bad.bytes.push_back(0);
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[24] = 0;
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[12] = 0xff;
  bad.bytes[13] = 0xff;
  assert(!pack.load(bad));
  bad = good;
  bad.bytes.resize(40);
  assert(!pack.load(bad));
  bad = fixture(false);
  assert(!pack.load(bad));  // refuse unapproved/draft
  // Do not reload a live Game: this explicitly exercises offline test mode.
  assert(pack.load(bad, true));
  // Invalid UTF-8 and an empty string inside the attribution block.
  bad = good;
  bad.bytes[34] = 0xc0;
  refreshCrc(bad);
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[32] = 0;
  bad.bytes[33] = 0;
  refreshCrc(bad);
  assert(!pack.load(bad));  // released pack without a title
  bad = good;
  bad.bytes[at.firstPrompt + 6] = 0xc0;
  refreshCrc(bad);
  assert(!pack.load(bad));
  bad = good;
  bad.bytes[at.secondPrompt + 2] = 3;  // pick 3 against two slots
  refreshCrc(bad);
  assert(!pack.load(bad));
  // Header/payload I/O failures and every truncated prefix preserve a good deck.
  assert(pack.load(input));
  const auto* prompts = pack.deck().prompts;
  const auto* answers = pack.deck().answers;
  for (uint32_t failAt : {0u, kHeaderBytes}) {
    bad = good;
    bad.failOffset = failAt;
    assert(!pack.load(bad));
    assert(pack.deck().prompts == prompts && pack.deck().answers == answers && pack.version() == 1);
  }
  for (size_t size = 0; size < good.bytes.size(); ++size) {
    bad = good;
    bad.bytes.resize(size);
    assert(!pack.load(bad));
    assert(pack.deck().prompts == prompts && pack.deck().answers == answers && pack.version() == 1);
  }
  std::puts("cardparty sd pack: game, no blank, pick2, CRC, UTF8, draft gate, malformed headers OK");
  categoriesAndFilter();
  return 0;
}
