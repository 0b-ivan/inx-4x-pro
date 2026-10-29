#pragma once
// Several SD packs played as one deck. No Arduino dependency: host-testable.
//
// One pack is passed through unchanged (same Deck, categories and CRC), so
// saved category switches and history of single-pack tables keep working.
// Several packs are joined by pointer: the combined arrays point into each
// pack's own text blob, nothing is copied. Categories of the same name are
// merged (adult if any pack says so, on by default only if all do), exact
// duplicate cards are kept once, and the set has its own CRC derived from
// the member packs, so its category switches are saved separately.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "CardPartySdPack.h"

namespace cardparty {
namespace sdpack {

constexpr uint8_t kMaxPacks = 8;

class DeckSet final {
 public:
  DeckSet() = default;
  DeckSet(const DeckSet&) = delete;
  DeckSet& operator=(const DeckSet&) = delete;

  // Atomic: if any pack is rejected or the union does not fit (more than 16
  // categories or 4096 cards of a kind, out of memory), the previously
  // loaded set stays as it was.
  bool load(ByteSource* const* sources, uint8_t n, bool allowDraft = false) {
    if (!n || n > kMaxPacks || !sources) return false;
    std::unique_ptr<Pack> packs[kMaxPacks];
    for (uint8_t i = 0; i < n; ++i) {
      packs[i].reset(new (std::nothrow) Pack());
      if (!packs[i] || !sources[i] || !packs[i]->load(*sources[i], allowDraft)) return false;
    }
    Merged merged;
    if (n > 1 && !merge(packs, n, merged)) return false;
    for (uint8_t i = 0; i < kMaxPacks; ++i) packs_[i] = std::move(packs[i]);
    count_ = n;
    prompts_ = std::move(merged.prompts);
    answers_ = std::move(merged.answers);
    promptMasks_ = std::move(merged.promptMasks);
    answerMasks_ = std::move(merged.answerMasks);
    if (n == 1) {
      const Pack& p = *packs_[0];
      deck_ = p.deck();
      categoryCount_ = p.categoryCount();
      for (uint8_t i = 0; i < kMaxCategories; ++i) categories_[i] = p.category(i);
      crc_ = p.crc();
      version_ = p.version();
    } else {
      deck_ = Deck{prompts_.get(), merged.promptCount, answers_.get(), merged.answerCount, promptMasks_.get(),
                   answerMasks_.get()};
      categoryCount_ = merged.categoryCount;
      for (uint8_t i = 0; i < kMaxCategories; ++i) categories_[i] = merged.categories[i];
      crc_ = setCrc(n);
      version_ = 0;
      for (uint8_t i = 0; i < n; ++i) version_ += packs_[i]->version();
    }
    describe();
    return true;
  }
  void clear() {
    for (auto& p : packs_) p.reset();
    prompts_.reset();
    answers_.reset();
    promptMasks_.reset();
    answerMasks_.reset();
    deck_ = Deck{};
    count_ = categoryCount_ = 0;
    crc_ = version_ = 0;
    title_[0] = license_[0] = holder_[0] = 0;
  }

  uint8_t packCount() const { return count_; }
  const Pack& pack(uint8_t i) const { return *packs_[i < count_ ? i : 0]; }
  const Deck& deck() const { return deck_; }
  uint32_t version() const { return version_; }
  uint32_t crc() const { return crc_; }
  bool ready() const { return deck_.promptsCount > 0 && deck_.answersCount > 0; }
  uint8_t categoryCount() const { return categoryCount_; }
  const Category& category(uint8_t i) const { return categories_[i < kMaxCategories ? i : 0]; }
  uint16_t promptMask(uint16_t i) const {
    return i < deck_.promptsCount && deck_.promptMasks ? deck_.promptMasks[i] : 0;
  }
  uint16_t answerMask(uint16_t i) const {
    return i < deck_.answersCount && deck_.answerMasks ? deck_.answerMasks[i] : 0;
  }
  // Titles, licenses and rights holders of all packs, " + " separated and
  // cut on a UTF-8 boundary to fit the X4 lines.
  const char* title() const { return title_; }
  const char* license() const { return license_; }
  const char* rightsHolder() const { return holder_; }

 private:
  struct Merged {
    std::unique_ptr<Prompt[]> prompts;
    std::unique_ptr<Answer[]> answers;
    std::unique_ptr<uint16_t[]> promptMasks, answerMasks;
    uint16_t promptCount = 0, answerCount = 0;
    Category categories[kMaxCategories] = {};
    uint8_t categoryCount = 0;
  };
  static uint32_t hash(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
  }
  static uint16_t remap(uint16_t mask, const uint8_t* map) {
    uint16_t out = 0;
    for (uint8_t b = 0; b < kMaxCategories; ++b)
      if (mask & (1u << b)) out = static_cast<uint16_t>(out | (1u << map[b]));
    return out;
  }
  // Appends one kind of card from every pack, skipping exact duplicates.
  template <class Card, class Text, class Mask>
  static bool join(const std::unique_ptr<Pack>* packs, uint8_t n, const uint8_t (*maps)[kMaxCategories], Text text,
                   Mask mask, uint16_t (*count)(const Deck&), const Card* (*cards)(const Deck&),
                   std::unique_ptr<Card[]>& out, std::unique_ptr<uint16_t[]>& outMasks, uint16_t& outCount) {
    uint32_t total = 0;
    for (uint8_t p = 0; p < n; ++p) total += count(packs[p]->deck());
    if (!total || total > 4096) return false;
    out.reset(new (std::nothrow) Card[total]);
    outMasks.reset(new (std::nothrow) uint16_t[total]);
    std::unique_ptr<uint32_t[]> hashes(new (std::nothrow) uint32_t[total]);
    if (!out || !outMasks || !hashes) return false;
    uint16_t used = 0;
    for (uint8_t p = 0; p < n; ++p) {
      const Deck& d = packs[p]->deck();
      for (uint16_t i = 0; i < count(d); ++i) {
        const Card& card = cards(d)[i];
        const uint32_t h = hash(text(card));
        bool dup = false;
        for (uint16_t j = 0; j < used && !dup; ++j)
          dup = hashes[j] == h && std::strcmp(text(out[j]), text(card)) == 0;
        if (dup) continue;
        hashes[used] = h;
        out[used] = card;
        outMasks[used] = remap(mask(*packs[p], i), maps[p]);
        ++used;
      }
    }
    outCount = used;
    return true;
  }
  static bool merge(const std::unique_ptr<Pack>* packs, uint8_t n, Merged& m) {
    uint8_t maps[kMaxPacks][kMaxCategories] = {};
    for (uint8_t p = 0; p < n; ++p)
      for (uint8_t c = 0; c < packs[p]->categoryCount(); ++c) {
        const Category& cat = packs[p]->category(c);
        uint8_t j = 0;
        while (j < m.categoryCount && std::strcmp(m.categories[j].name, cat.name) != 0) ++j;
        if (j == m.categoryCount) {
          if (m.categoryCount == kMaxCategories) return false;
          m.categories[m.categoryCount++] = cat;
        } else {
          m.categories[j].adult = m.categories[j].adult || cat.adult;
          m.categories[j].defaultOn = m.categories[j].defaultOn && cat.defaultOn;
        }
        maps[p][c] = j;
      }
    for (uint8_t j = 0; j < m.categoryCount; ++j)
      if (m.categories[j].adult) m.categories[j].defaultOn = false;
    return join<Prompt>(
               packs, n, maps, [](const Prompt& c) { return c.text; },
               [](const Pack& pk, uint16_t i) { return pk.promptMask(i); },
               [](const Deck& d) { return d.promptsCount; }, [](const Deck& d) { return d.prompts; }, m.prompts,
               m.promptMasks, m.promptCount) &&
           join<Answer>(
               packs, n, maps, [](const Answer& c) { return c.text; },
               [](const Pack& pk, uint16_t i) { return pk.answerMask(i); },
               [](const Deck& d) { return d.answersCount; }, [](const Deck& d) { return d.answers; }, m.answers,
               m.answerMasks, m.answerCount) &&
           m.answerCount >= 32;
  }
  // Order-independent: the same packs in any order are the same set.
  uint32_t setCrc(uint8_t n) const {
    uint32_t crcs[kMaxPacks] = {};
    for (uint8_t i = 0; i < n && i < kMaxPacks; ++i) {
      uint8_t j = i;
      for (; j > 0 && crcs[j - 1] > packs_[i]->crc(); --j) crcs[j] = crcs[j - 1];  // insertion sort
      crcs[j] = packs_[i]->crc();
    }
    uint8_t bytes[4 + 4 * kMaxPacks] = {'S', 'E', 'T', n};
    for (uint8_t i = 0; i < n; ++i)
      for (uint8_t b = 0; b < 4; ++b) bytes[4 + 4 * i + b] = static_cast<uint8_t>(crcs[i] >> (8 * b));
    return crc32(bytes, 4 + 4u * n);
  }
  static void append(char* out, size_t size, const char* text) {
    size_t used = std::strlen(out);
    if (used && used + 3 < size) {
      std::memcpy(out + used, " + ", 3);
      used += 3;
    }
    size_t n = std::strlen(text);
    if (used + n >= size) {
      n = size - 1 - used;
      while (n && (static_cast<uint8_t>(text[n]) & 0xc0) == 0x80) --n;  // whole UTF-8 characters only
    }
    std::memcpy(out + used, text, n);
    out[used + n] = 0;
  }
  void describe() {
    title_[0] = license_[0] = holder_[0] = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      append(title_, sizeof(title_), packs_[i]->title());
      bool seen = false;
      for (uint8_t j = 0; j < i; ++j) seen = seen || std::strcmp(packs_[j]->license(), packs_[i]->license()) == 0;
      if (!seen) append(license_, sizeof(license_), packs_[i]->license());
      append(holder_, sizeof(holder_), packs_[i]->rightsHolder());
    }
  }

  std::unique_ptr<Pack> packs_[kMaxPacks];
  uint8_t count_ = 0;
  std::unique_ptr<Prompt[]> prompts_;
  std::unique_ptr<Answer[]> answers_;
  std::unique_ptr<uint16_t[]> promptMasks_, answerMasks_;
  Deck deck_{};
  Category categories_[kMaxCategories] = {};
  uint8_t categoryCount_ = 0;
  uint32_t crc_ = 0, version_ = 0;
  char title_[96] = {};
  char license_[96] = {};
  char holder_[320] = {};
};

}  // namespace sdpack
}  // namespace cardparty
