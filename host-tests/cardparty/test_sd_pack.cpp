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
  const struct {
    uint8_t flags;
    const char* name;
  } cats[] = {
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
  const Deck& deck = pack.deck();
  // FSK 18 off: the Böse+FSK 18 prompt and the 10 adult answers are out.
  assert(categories::promptsIn(deck, defaults) == 1 && categories::answersIn(deck, defaults) == 90);
  assert(categories::promptsIn(deck, 0b111) == 2 && categories::answersIn(deck, 0b111) == 100);
  assert(categories::promptsIn(deck, 0) == 1 && categories::answersIn(deck, 0) == 60);  // untagged only
  assert(categories::cardsIn(pack, 1, true) == 30 && categories::cardsIn(pack, 2, false) == 1);

  // A full table at hand 5 needs 8*5 + 7*maxDraw + 3 distinct answers.
  assert(categories::requiredAnswers(deck, defaults, 5) == 43);
  assert(categories::playable(deck, defaults, 5));
  Deck tiny = deck;
  tiny.answerMasks = nullptr;
  tiny.answersCount = 42;
  assert(!categories::playable(tiny, defaults, 5));

  // The game plays the whole pack and filters when it draws: card ids are
  // pack ids, so switching a category mid-game keeps every dealt card valid.
  Game game;
  assert(game.configure(deck, Rules{5, 10, true}, 7));
  game.setEnabled(defaults);
  assert(game.join("A") == 0 && game.join("B") == 1 && game.join("C") == 2);
  assert(game.start());
  for (int round = 0; round < 8; ++round) {
    assert(game.publicView().prompt == 0);  // the only prompt without FSK 18
    for (uint8_t seat = 0; seat < 3; ++seat) {
      HandView hand{};
      assert(game.privateHand(seat, hand));
      for (uint8_t i = 0; i < hand.size; ++i) assert(hand.cards[i] < 90);
    }
    const auto v = game.publicView();
    const uint8_t slot[1] = {0};
    for (uint8_t seat = 0; seat < 3; ++seat)
      if (seat != v.czar) assert(game.submit(seat, slot, 1));
    assert(game.judge(v.czar, 0) && game.nextRound());
  }
  // Untagged only from now on: new draws come from the first 60 answers.
  game.setEnabled(0);
  const auto before = game.publicView();
  const uint8_t slot[1] = {0};
  for (uint8_t seat = 0; seat < 3; ++seat)
    if (seat != before.czar) assert(game.submit(seat, slot, 1));
  assert(game.judge(before.czar, 0) && game.nextRound());
  for (uint8_t seat = 0; seat < 3; ++seat) {
    if (seat == before.czar) continue;  // played nothing, drew nothing
    HandView hand{};
    assert(game.privateHand(seat, hand));
    assert(hand.cards[hand.size - 1] < 60);  // the card drawn after the switch
  }

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
  assert(categories::answersIn(pack.deck(), 0) == 100);
  std::puts("cardparty sd pack: categories, adult off by default, filter, min answers, saved selection OK");
}

static void deckListAndSelections() {
  // The deck list reads header and title only, whatever the body holds.
  Info info;
  Buffer input = fixture();
  assert(peek(input, info) && info.released && info.prompts == 2 && info.answers == 100 && info.categories == 3);
  assert(std::strcmp(info.title, "QA Deck") == 0);
  Buffer draft = fixture(false);
  assert(peek(draft, info) && !info.released);
  Buffer bad = input;
  bad.bytes.back() ^= 0x20;  // body CRC wrong: listed, load() still refuses it
  Pack pack;
  assert(peek(bad, info) && !pack.load(bad));
  bad = input;
  bad.bytes[0] = '?';
  assert(!peek(bad, info));
  bad = input;
  bad.bytes.resize(33);
  assert(!peek(bad, info));
  bad = input;
  bad.bytes[34] = 0xc0;
  assert(!peek(bad, info));
  // A long UTF-8 title is cut on a character boundary.
  std::string title;
  for (int i = 0; i < 30; ++i) title += "ü";
  Buffer longTitle;
  longTitle.bytes = input.bytes;
  longTitle.bytes.erase(longTitle.bytes.begin() + 32, longTitle.bytes.begin() + 32 + 2 + 7 + 1);
  std::vector<uint8_t> head;
  str(head, title);
  longTitle.bytes.insert(longTitle.bytes.begin() + 32, head.begin(), head.end());
  assert(peek(longTitle, info) && std::strlen(info.title) == 46 &&
         validText(reinterpret_cast<const uint8_t*>(info.title), 46));

  // clear() frees the cards and the pack can load again.
  assert(pack.load(input) && pack.ready());
  pack.clear();
  assert(!pack.ready() && pack.categoryCount() == 0 && std::strcmp(pack.title(), "") == 0);
  assert(pack.load(input) && pack.ready());

  // One selection record per pack: storing replaces its own, keeps others.
  uint8_t file[categories::kSelectionBytes * categories::kSelectionSlots] = {};
  size_t len = categories::store(file, 0, 0x11, 0b01);
  len = categories::store(file, len, 0x22, 0b10);
  len = categories::store(file, len, 0x11, 0b11);
  assert(len == 2 * categories::kSelectionBytes);
  uint16_t mask = 0;
  assert(categories::find(file, len, 0x11, 3, mask) && mask == 0b11);
  assert(categories::find(file, len, 0x22, 3, mask) && mask == 0b10);
  assert(!categories::find(file, len, 0x33, 3, mask));
  // A single-record file from before still reads.
  assert(categories::find(file, categories::kSelectionBytes, 0x22, 3, mask) && mask == 0b10);
  // Full file: the oldest record goes.
  for (uint32_t crc = 0x100; crc < 0x100 + categories::kSelectionSlots; ++crc)
    len = categories::store(file, len, crc, 1);
  assert(len == sizeof(file));
  assert(!categories::find(file, len, 0x22, 3, mask) && !categories::find(file, len, 0x11, 3, mask));
  assert(categories::find(file, len, 0x100, 3, mask) && categories::find(file, len, 0x107, 3, mask));
  // Junk records are dropped on the next store.
  file[0] = 'X';
  len = categories::store(file, len, 0x200, 2);
  assert(!categories::find(file, len, 0x100, 3, mask) && categories::find(file, len, 0x200, 3, mask) && mask == 2);
  std::puts("cardparty sd pack: deck list peek, clear, per-pack selections OK");
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
  deckListAndSelections();
  return 0;
}
