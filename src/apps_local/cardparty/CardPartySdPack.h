#pragma once
// Versioned read-only CAW SD pack. No Arduino dependency: host-testable.
// A release flag is only a safety gate, NOT proof of rights or authenticity.
//
// Format 2 (little endian):
//   header  32 bytes: magic "XCAWPK2\0", u16 format, u16 flags, u16 prompts,
//           u16 answers, u32 body bytes, u32 body CRC-32, u32 deck version,
//           u8 categories, 3 zero bytes
//   meta    title, license, license URL, rights holder, source URL:
//           each u16 length + UTF-8 + NUL
//   cats    per category: u8 flags (1 = on by default, 2 = adult), u16 length
//           + UTF-8 name + NUL
//   prompts u16 length, u8 pick, u8 draw, u16 category mask, UTF-8, NUL
//   answers u16 length, u16 category mask, UTF-8, NUL
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "CardPartyCore.h"

namespace cardparty {
namespace sdpack {
constexpr uint32_t kMaxPackBytes = 192u * 1024u;
constexpr uint16_t kMaxPromptBytes = 384, kMaxAnswerBytes = 160;
constexpr uint16_t kMaxMetaBytes = 200, kMaxCategoryNameBytes = 24;
constexpr uint8_t kMaxCategories = 16;
constexpr uint32_t kHeaderBytes = 32;
constexpr uint16_t kFormatVersion = 2, kReleasedFlag = 1;
constexpr uint8_t kCategoryDefaultOn = 1, kCategoryAdult = 2;
constexpr uint8_t kMetaFields = 5;

struct Category {
  const char* name = nullptr;
  bool defaultOn = false, adult = false;
};

class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual uint32_t size() const = 0;
  // An incomplete read returns false; partial data may never be accepted.
  virtual bool read(uint32_t offset, void* dst, uint32_t count) = 0;
};
inline uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
inline uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
inline bool validText(const uint8_t* p, size_t len) {
  if (!len) return false;
  for (size_t i = 0; i < len;) {
    const uint8_t c = p[i];
    if (c < 0x20 || c == 0x7f) return false;
    if (c < 0x80) {
      ++i;
      continue;
    }
    unsigned extra = 0;
    if (c >= 0xc2 && c <= 0xdf)
      extra = 1;
    else if (c >= 0xe0 && c <= 0xef)
      extra = 2;
    else if (c >= 0xf0 && c <= 0xf4)
      extra = 3;
    else
      return false;
    if (i + extra >= len) return false;
    for (unsigned j = 1; j <= extra; ++j)
      if ((p[i + j] & 0xc0) != 0x80) return false;
    if ((c == 0xe0 && p[i + 1] < 0xa0) || (c == 0xed && p[i + 1] >= 0xa0) || (c == 0xf0 && p[i + 1] < 0x90) ||
        (c == 0xf4 && p[i + 1] > 0x8f))
      return false;
    i += extra + 1;
  }
  return true;
}
inline uint8_t placeholders(const char* str, uint16_t len) {
  uint8_t n = 0;
  for (uint16_t i = 0; i + 3 < len; ++i)
    if (std::memcmp(str + i, "____", 4) == 0) {
      if (n == 255) return n;
      ++n;
      i += 3;
    }
  return n;
}

// What the deck list shows for a pack file, read from its header and title
// without loading or checking the body. load() still decides.
struct Info {
  char title[48] = {};
  uint16_t prompts = 0, answers = 0;
  uint8_t categories = 0;
  bool released = false;
};
inline bool peek(ByteSource& source, Info& info) {
  info = Info{};
  const uint32_t size = source.size();
  uint8_t header[kHeaderBytes + 2] = {};
  if (size < sizeof(header) || size > kMaxPackBytes || !source.read(0, header, sizeof(header))) return false;
  const uint8_t magic[8] = {'X', 'C', 'A', 'W', 'P', 'K', '2', 0};
  if (std::memcmp(magic, header, 8) != 0 || read16(header + 8) != kFormatVersion) return false;
  info.released = (read16(header + 10) & kReleasedFlag) != 0;
  info.prompts = read16(header + 12);
  info.answers = read16(header + 14);
  info.categories = header[28];
  const uint16_t n = read16(header + kHeaderBytes);
  if (n > kMaxMetaBytes || size - sizeof(header) < n) return false;
  uint8_t title[kMaxMetaBytes] = {};
  if (n && (!source.read(sizeof(header), title, n) || !validText(title, n))) return false;
  // Cut on a UTF-8 boundary so a long title stays drawable.
  size_t keep = n < sizeof(info.title) - 1 ? n : sizeof(info.title) - 1;
  while (keep && keep < n && (title[keep] & 0xc0) == 0x80) --keep;
  std::memcpy(info.title, title, keep);
  return true;
}

class Pack final {
 public:
  Pack() = default;
  Pack(const Pack&) = delete;
  Pack& operator=(const Pack&) = delete;
  // Call only before Game::configure; no hot reload while game uses text ptrs.
  bool load(ByteSource& source, bool allowDraft = false) {
    const uint32_t size = source.size();
    if (size < kHeaderBytes || size > kMaxPackBytes) return false;
    uint8_t header[kHeaderBytes] = {};
    if (!source.read(0, header, kHeaderBytes)) return false;
    const uint8_t magic[8] = {'X', 'C', 'A', 'W', 'P', 'K', '2', 0};
    if (std::memcmp(magic, header, 8) != 0 || read16(header + 8) != kFormatVersion) return false;
    const uint16_t flags = read16(header + 10);
    const bool released = (flags & kReleasedFlag) != 0;
    if ((flags & ~kReleasedFlag) != 0 || (!allowDraft && !released)) return false;
    const uint16_t pCount = read16(header + 12), aCount = read16(header + 14);
    if (!pCount || pCount > 4096 || aCount < 32 || aCount > 4096) return false;
    const uint32_t bytes = read32(header + 16);
    const uint8_t cCount = header[28];
    if (bytes != size - kHeaderBytes || read32(header + 24) == 0 || cCount > kMaxCategories || header[29] ||
        header[30] || header[31])
      return false;
    std::unique_ptr<uint8_t[]> blob(new (std::nothrow) uint8_t[bytes]);
    std::unique_ptr<Prompt[]> prompts(new (std::nothrow) Prompt[pCount]);
    std::unique_ptr<Answer[]> answers(new (std::nothrow) Answer[aCount]);
    std::unique_ptr<uint16_t[]> promptMasks(new (std::nothrow) uint16_t[pCount]);
    std::unique_ptr<uint16_t[]> answerMasks(new (std::nothrow) uint16_t[aCount]);
    if (!blob || !prompts || !answers || !promptMasks || !answerMasks || !source.read(kHeaderBytes, blob.get(), bytes))
      return false;
    if (crc32(blob.get(), bytes) != read32(header + 20)) return false;
    uint32_t offset = 0;
    // Length-prefixed, NUL-terminated UTF-8; empty only where allowed.
    auto text = [&](uint16_t maxBytes, bool allowEmpty, const char*& out) -> bool {
      if (bytes - offset < 2) return false;
      const uint16_t n = read16(blob.get() + offset);
      offset += 2;
      if (n > maxBytes || bytes - offset <= n || blob[offset + n] != 0) return false;
      if (n ? !validText(blob.get() + offset, n) : !allowEmpty) return false;
      out = reinterpret_cast<const char*>(blob.get() + offset);
      offset += n + 1;
      return true;
    };
    const char* meta[kMetaFields] = {};
    for (uint8_t i = 0; i < kMetaFields; ++i)
      // A released pack must carry its attribution; drafts may leave it out.
      if (!text(kMaxMetaBytes, !released, meta[i])) return false;
    Category categories[kMaxCategories] = {};
    for (uint8_t i = 0; i < cCount; ++i) {
      if (bytes - offset < 1) return false;
      const uint8_t catFlags = blob[offset++];
      if (catFlags & ~(kCategoryDefaultOn | kCategoryAdult)) return false;
      if (!text(kMaxCategoryNameBytes, false, categories[i].name)) return false;
      categories[i].adult = (catFlags & kCategoryAdult) != 0;
      // Adult content is never on until the host switches it on.
      categories[i].defaultOn = (catFlags & kCategoryDefaultOn) != 0 && !categories[i].adult;
    }
    const uint16_t validMask = static_cast<uint16_t>((1u << cCount) - 1u);
    for (uint16_t i = 0; i < pCount; ++i) {
      if (bytes - offset < 6) return false;
      const uint16_t n = read16(blob.get() + offset);
      const uint8_t pick = blob[offset + 2], draw = blob[offset + 3];
      const uint16_t mask = read16(blob.get() + offset + 4);
      offset += 6;
      if (!n || n > kMaxPromptBytes || bytes - offset <= n || pick < 1 || pick > 3 || draw > 2 || (mask & ~validMask) ||
          blob[offset + n] != 0 || !validText(blob.get() + offset, n))
        return false;
      const char* str = reinterpret_cast<const char*>(blob.get() + offset);
      const uint8_t placeholdersFound = placeholders(str, n);
      // No slots: question stands alone; answers display separately.
      if (placeholdersFound && placeholdersFound != pick) return false;
      prompts[i] = Prompt{str, pick, draw};
      promptMasks[i] = mask;
      offset += n + 1;
    }
    for (uint16_t i = 0; i < aCount; ++i) {
      if (bytes - offset < 4) return false;
      const uint16_t n = read16(blob.get() + offset);
      const uint16_t mask = read16(blob.get() + offset + 2);
      offset += 4;
      if (!n || n > kMaxAnswerBytes || bytes - offset <= n || (mask & ~validMask) || blob[offset + n] != 0 ||
          !validText(blob.get() + offset, n))
        return false;
      const char* str = reinterpret_cast<const char*>(blob.get() + offset);
      if (placeholders(str, n)) return false;
      answers[i] = Answer{str};
      answerMasks[i] = mask;
      offset += n + 1;
    }
    if (offset != bytes) return false;
    // Atomic commit: corrupt or truncated new pack never replaces a good one.
    blob_ = std::move(blob);
    prompts_ = std::move(prompts);
    answers_ = std::move(answers);
    promptMasks_ = std::move(promptMasks);
    answerMasks_ = std::move(answerMasks);
    deck_ = Deck{prompts_.get(), pCount, answers_.get(), aCount};
    for (uint8_t i = 0; i < kMetaFields; ++i) meta_[i] = meta[i];
    for (uint8_t i = 0; i < kMaxCategories; ++i) categories_[i] = categories[i];
    categoryCount_ = cCount;
    version_ = read32(header + 24);
    crc_ = read32(header + 20);
    return true;
  }
  // Frees the cards; deck() is empty until the next load().
  void clear() {
    blob_.reset();
    prompts_.reset();
    answers_.reset();
    promptMasks_.reset();
    answerMasks_.reset();
    deck_ = Deck{};
    for (uint8_t i = 0; i < kMetaFields; ++i) meta_[i] = "";
    categoryCount_ = 0;
    version_ = crc_ = 0;
  }
  const Deck& deck() const { return deck_; }
  uint32_t version() const { return version_; }
  // Identifies the exact pack content, e.g. to key a saved category choice.
  uint32_t crc() const { return crc_; }
  bool ready() const { return deck_.promptsCount > 0 && deck_.answersCount > 0; }
  uint8_t categoryCount() const { return categoryCount_; }
  const Category& category(uint8_t i) const { return categories_[i < kMaxCategories ? i : 0]; }
  uint16_t promptMask(uint16_t i) const { return i < deck_.promptsCount ? promptMasks_[i] : 0; }
  uint16_t answerMask(uint16_t i) const { return i < deck_.answersCount ? answerMasks_[i] : 0; }
  const char* title() const { return meta_[0]; }
  const char* license() const { return meta_[1]; }
  const char* licenseUrl() const { return meta_[2]; }
  const char* rightsHolder() const { return meta_[3]; }
  const char* sourceUrl() const { return meta_[4]; }

 private:
  std::unique_ptr<uint8_t[]> blob_;
  std::unique_ptr<Prompt[]> prompts_;
  std::unique_ptr<Answer[]> answers_;
  std::unique_ptr<uint16_t[]> promptMasks_;
  std::unique_ptr<uint16_t[]> answerMasks_;
  Deck deck_{};
  const char* meta_[kMetaFields] = {"", "", "", "", ""};
  Category categories_[kMaxCategories] = {};
  uint8_t categoryCount_ = 0;
  uint32_t version_ = 0, crc_ = 0;
};

}  // namespace sdpack
}  // namespace cardparty
