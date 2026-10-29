// Several SD packs as one deck: merge, duplicates, categories, CRC, atomicity.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "CardPartyCategories.h"
#include "CardPartyCore.h"
#include "CardPartyDeckSet.h"

using namespace cardparty;
using namespace cardparty::sdpack;

namespace {
int failures = 0, checks = 0;
#define CHECK(c)                                                       \
  do {                                                                 \
    ++checks;                                                          \
    if (!(c)) {                                                        \
      ++failures;                                                      \
      std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
    }                                                                  \
  } while (0)

struct Buffer : ByteSource {
  std::vector<uint8_t> bytes;
  uint32_t size() const override { return static_cast<uint32_t>(bytes.size()); }
  bool read(uint32_t offset, void* dst, uint32_t n) override {
    if (offset > bytes.size() || n > bytes.size() - offset) return false;
    std::memcpy(dst, bytes.data() + offset, n);
    return true;
  }
};
void u16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void u32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void str(std::vector<uint8_t>& v, const std::string& s) {
  u16(v, static_cast<uint16_t>(s.size()));
  v.insert(v.end(), s.begin(), s.end());
  v.push_back(0);
}

struct Cat {
  std::string name;
  uint8_t flags;
};
struct Card {
  std::string text;
  uint16_t mask;
  uint8_t pick = 1;
};

// Released format-2 pack, as build_sd_pack.py writes it.
Buffer pack(const std::string& title, const std::string& license, const std::vector<Cat>& cats,
            const std::vector<Card>& prompts, const std::vector<Card>& answers, uint32_t version = 1) {
  std::vector<uint8_t> body;
  for (const std::string& meta :
       {title, license, std::string("https://example.org/l"), title + " Autoren", std::string("https://example.org")})
    str(body, meta);
  for (const Cat& c : cats) {
    body.push_back(c.flags);
    str(body, c.name);
  }
  for (const Card& p : prompts) {
    u16(body, static_cast<uint16_t>(p.text.size()));
    body.push_back(p.pick);
    body.push_back(p.pick == 3 ? 2 : 0);
    u16(body, p.mask);
    body.insert(body.end(), p.text.begin(), p.text.end());
    body.push_back(0);
  }
  for (const Card& a : answers) {
    u16(body, static_cast<uint16_t>(a.text.size()));
    u16(body, a.mask);
    body.insert(body.end(), a.text.begin(), a.text.end());
    body.push_back(0);
  }
  Buffer out;
  out.bytes.insert(out.bytes.end(), {'X', 'C', 'A', 'W', 'P', 'K', '2', 0});
  u16(out.bytes, 2);
  u16(out.bytes, 1);
  u16(out.bytes, static_cast<uint16_t>(prompts.size()));
  u16(out.bytes, static_cast<uint16_t>(answers.size()));
  u32(out.bytes, static_cast<uint32_t>(body.size()));
  u32(out.bytes, crc32(body.data(), body.size()));
  u32(out.bytes, version);
  out.bytes.push_back(static_cast<uint8_t>(cats.size()));
  out.bytes.insert(out.bytes.end(), {0, 0, 0});
  out.bytes.insert(out.bytes.end(), body.begin(), body.end());
  return out;
}

// A: Basis (on), FSK 18 (adult). 2 prompts, 60 answers; answers 50..59 FSK 18.
Buffer packA() {
  std::vector<Card> prompts = {{"Was ist ____?", 0}, {"Erst ____, dann ____.", 0b10, 2}};
  std::vector<Card> answers;
  for (int i = 0; i < 60; ++i)
    answers.push_back({"A-Antwort " + std::to_string(i), static_cast<uint16_t>(i >= 50 ? 0b10 : 0)});
  answers[0].text = "Gemeinsame Karte";
  return pack("Deutsch", "CC-BY-NC-SA-2.0", {{"Basis", kCategoryDefaultOn}, {"FSK 18", kCategoryAdult}}, prompts,
              answers);
}
// B: Schwarz (on), FSK 18 (adult), Basis (off here). 1 shared prompt.
Buffer packB() {
  std::vector<Card> prompts = {{"Was ist ____?", 0b001}, {"Opas letzte Worte: ____.", 0b001}};
  std::vector<Card> answers;
  for (int i = 0; i < 40; ++i)
    answers.push_back({"B-Antwort " + std::to_string(i), static_cast<uint16_t>(i < 20 ? 0b011 : 0b001)});
  answers[39].text = "Gemeinsame Karte";
  return pack("Nach Mitternacht", "CC0-1.0",
              {{"Schwarz", kCategoryDefaultOn}, {"FSK 18", kCategoryAdult}, {"Basis", 0}}, prompts, answers);
}

void singlePassesThrough() {
  Buffer a = packA();
  Pack alone;
  CHECK(alone.load(a));
  DeckSet set;
  ByteSource* src[] = {&a};
  CHECK(set.load(src, 1));
  CHECK(set.packCount() == 1 && set.crc() == alone.crc());
  CHECK(set.deck().answersCount == 60 && set.deck().promptsCount == 2);
  CHECK(set.categoryCount() == 2 && std::strcmp(set.category(1).name, "FSK 18") == 0);
  CHECK(std::strcmp(set.title(), "Deutsch") == 0 && std::strcmp(set.license(), "CC-BY-NC-SA-2.0") == 0);
  CHECK(categories::defaults(set) == 0b01);
}

void twoPacksMerge() {
  Buffer a = packA(), b = packB();
  DeckSet set;
  ByteSource* ab[] = {&a, &b};
  CHECK(set.load(ab, 2));
  const Deck& d = set.deck();
  // One shared prompt and one shared answer are kept once.
  CHECK(d.promptsCount == 3 && d.answersCount == 99);
  CHECK(set.categoryCount() == 3);
  CHECK(std::strcmp(set.category(0).name, "Basis") == 0 && !set.category(0).defaultOn);  // B says off
  CHECK(std::strcmp(set.category(1).name, "FSK 18") == 0 && set.category(1).adult && !set.category(1).defaultOn);
  CHECK(std::strcmp(set.category(2).name, "Schwarz") == 0 && set.category(2).defaultOn);
  // B's "Schwarz + FSK 18" answer: Schwarz is bit 2, FSK 18 bit 1 in the set.
  bool found = false;
  for (uint16_t i = 0; i < d.answersCount; ++i)
    if (std::strcmp(d.answers[i].text, "B-Antwort 3") == 0) found = set.answerMask(i) == 0b110;
  CHECK(found);
  // The shared answer keeps pack A's (first) categories.
  for (uint16_t i = 0; i < d.answersCount; ++i)
    if (std::strcmp(d.answers[i].text, "Gemeinsame Karte") == 0) CHECK(set.answerMask(i) == 0);
  CHECK(std::strcmp(set.title(), "Deutsch + Nach Mitternacht") == 0);
  CHECK(std::strcmp(set.license(), "CC-BY-NC-SA-2.0 + CC0-1.0") == 0);
  // The set has its own CRC, the same in either order.
  Pack pa, pb;
  CHECK(pa.load(a) && pb.load(b));
  CHECK(set.crc() != pa.crc() && set.crc() != pb.crc());
  DeckSet swapped;
  ByteSource* ba[] = {&b, &a};
  CHECK(swapped.load(ba, 2) && swapped.crc() == set.crc());
  // Same pack twice is just the pack's cards once.
  DeckSet twice;
  ByteSource* aa[] = {&a, &a};
  CHECK(twice.load(aa, 2) && twice.deck().answersCount == 60 && twice.deck().promptsCount == 2);
}

void playsAMatchWithFilters() {
  Buffer a = packA(), b = packB();
  DeckSet set;
  ByteSource* ab[] = {&a, &b};
  CHECK(set.load(ab, 2));
  const uint16_t enabled = categories::defaults(set);  // Schwarz only: Basis off in B, FSK 18 adult
  CHECK(enabled == 0b100);
  const uint16_t all = 0b111;
  CHECK(categories::playable(set.deck(), all, 5));
  Game g;
  CHECK(g.configure(set.deck(), Rules{5, 6, true}, 3));
  g.setEnabled(all & ~0b010);  // everything but FSK 18
  for (const char* n : {"A", "B", "C"}) g.join(n);
  CHECK(g.start());
  std::set<std::string> seen;
  for (int r = 0; r < 6; ++r) {
    const auto v = g.publicView();
    CHECK((set.promptMask(v.prompt) & 0b010) == 0);
    for (uint8_t s = 0; s < 3; ++s) {
      HandView h{};
      g.privateHand(s, h);
      for (uint8_t i = 0; i < h.size; ++i) {
        CHECK((set.answerMask(h.cards[i]) & 0b010) == 0);  // no FSK 18 while it is off
        seen.insert(set.deck().answers[h.cards[i]].text);
      }
    }
    const uint8_t slot[3] = {0, 1, 2};
    for (uint8_t s = 0; s < 3; ++s)
      if (s != v.czar) g.submit(s, slot, v.pick);
    CHECK(g.judge(v.czar, 0));
    CHECK(g.nextRound());
  }
  CHECK(g.phase() == Phase::Finished);
  bool fromA = false, fromB = false;
  for (const auto& t : seen) {
    fromA = fromA || t.rfind("A-", 0) == 0;
    fromB = fromB || t.rfind("B-", 0) == 0;
  }
  CHECK(fromA && fromB);  // cards from both packs reach the table
}

// The full set: eight distinct packs sharing one category name merge into
// one deck, in any order; titles stay cut on whole characters.
void eightPacks() {
  static_assert(kMaxPacks == 8, "update this test with the pack limit");
  std::vector<Buffer> packs;
  for (int p = 0; p < kMaxPacks; ++p) {
    std::vector<Card> answers;
    for (int i = 0; i < 40; ++i) answers.push_back({"P" + std::to_string(p) + "-Antwort " + std::to_string(i), 0b1});
    answers[0].text = "Überall gleich";
    packs.push_back(pack("Päckchen Nummer " + std::to_string(p), "CC0-1.0", {{"Basis", kCategoryDefaultOn}},
                         {{"Frage " + std::to_string(p) + " ____", 0b1}}, answers));
  }
  ByteSource* all[kMaxPacks] = {};
  ByteSource* reversed[kMaxPacks] = {};
  for (int p = 0; p < kMaxPacks; ++p) {
    all[p] = &packs[p];
    reversed[kMaxPacks - 1 - p] = &packs[p];
  }
  DeckSet set;
  CHECK(set.load(all, kMaxPacks));
  CHECK(set.packCount() == kMaxPacks);
  CHECK(set.deck().promptsCount == kMaxPacks);
  CHECK(set.deck().answersCount == kMaxPacks * 39 + 1);  // the shared card once
  CHECK(set.categoryCount() == 1 && categories::defaults(set) == 0b1);
  CHECK(std::strlen(set.title()) < 96);
  CHECK(validText(reinterpret_cast<const uint8_t*>(set.title()), std::strlen(set.title())));
  DeckSet backwards;
  CHECK(backwards.load(reversed, kMaxPacks) && backwards.crc() == set.crc());
  Game g;
  CHECK(g.configure(set.deck(), Rules{5, 8, true}, 7));
}

void failureKeepsPreviousSet() {
  Buffer a = packA(), b = packB();
  DeckSet set;
  ByteSource* ab[] = {&a, &b};
  CHECK(set.load(ab, 2));
  const uint32_t crc = set.crc();
  const char* firstAnswer = set.deck().answers[0].text;
  Buffer broken = packB();
  broken.bytes[40] ^= 0xff;  // body CRC no longer matches
  ByteSource* bad[] = {&a, &broken};
  CHECK(!set.load(bad, 2));
  CHECK(set.crc() == crc && set.deck().answersCount == 99 && set.deck().answers[0].text == firstAnswer);
  // Seventeen distinct category names do not fit the 16-bit masks.
  std::vector<Cat> many;
  for (int i = 0; i < 15; ++i) many.push_back({"K" + std::to_string(i), kCategoryDefaultOn});
  std::vector<Card> answers;
  for (int i = 0; i < 40; ++i) answers.push_back({"C " + std::to_string(i), 0});
  Buffer c = pack("Viele", "CC0-1.0", many, {{"X ____", 0}}, answers);
  ByteSource* ac[] = {&a, &c};
  CHECK(!set.load(ac, 2));
  CHECK(set.crc() == crc);
  ByteSource* none[kMaxPacks + 1] = {};
  for (uint8_t i = 0; i <= kMaxPacks; ++i) none[i] = i % 2 ? &b : &a;
  CHECK(!set.load(none, kMaxPacks + 1));  // at most kMaxPacks packs
  CHECK(!set.load(none, 0));
  set.clear();
  CHECK(set.packCount() == 0 && !set.ready());
}
}  // namespace

int main() {
  singlePassesThrough();
  twoPacksMerge();
  playsAMatchWithFilters();
  eightPacks();
  failureKeepsPreviousSet();
  std::printf("cardparty deck set: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
