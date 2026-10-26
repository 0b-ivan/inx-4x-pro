#include "AwsStudyPack.h"

#include <cstring>
#include <limits>

namespace trivia {
namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

bool AwsPack::open(ByteSource& source) {
  source_ = nullptr;
  count_ = 0;
  uint8_t header[16] = {};
  if (!source.read(0, header, sizeof(header))) return false;
  if (std::memcmp(header, kAwsMagic, sizeof(kAwsMagic)) != 0 ||
      u16(header + 8) != kAwsVersion || header[10] != 0 || header[11] != 0) return false;
  const uint32_t n = u32(header + 12);
  if (n == 0 || n > 100000) return false;
  const uint64_t base = 16ull + (static_cast<uint64_t>(n) + 1) * 4;
  if (base > source.size()) return false;
  uint8_t sentinel[4] = {};
  if (!source.read(16 + 4 * n, sentinel, 4)) return false;
  if (base + u32(sentinel) != source.size()) return false;  // reject torn and trailing garbage
  source_ = &source;
  count_ = n;
  indexAt_ = 16;
  base_ = static_cast<uint32_t>(base);
  return true;
}

bool AwsPack::extent(const uint32_t index, uint32_t& absolute, uint32_t& length) const {
  if (source_ == nullptr || index >= count_) return false;
  uint8_t pair[8];
  if (!source_->read(indexAt_ + index * 4u, pair, sizeof(pair))) return false;
  const uint32_t start = u32(pair);
  const uint32_t end = u32(pair + 4);
  if (end <= start || end - start > kAwsMaxRecord) return false;
  if (static_cast<uint64_t>(base_) + end > source_->size()) return false;
  absolute = base_ + start;
  length = end - start;
  return true;
}

bool AwsPack::domainAt(const uint32_t index, uint8_t& domain) const {
  uint32_t at, length;
  if (!extent(index, at, length) || length < 4) return false;
  uint8_t h[4];
  if (!source_->read(at, h, sizeof(h)) || h[0] < 1 || h[0] > kAwsDomains) return false;
  domain = h[0];
  return true;
}

bool AwsPack::read(const uint32_t index, AwsQuestion& out) const {
  uint32_t at, length;
  if (!extent(index, at, length) || length < 4) return false;
  // Read directly into the question's owned buffer: no 12KiB stack scratch
  // and no extra temporary heap allocation on the ESP32-S3.
  if (!source_->read(at, out.bytes_, length)) return false;
  const uint8_t* h = reinterpret_cast<const uint8_t*>(out.bytes_);
  const uint8_t domain = h[0], options = h[1], correct = h[2], reserved = h[3];
  if (domain < 1 || domain > kAwsDomains || options < 2 ||
      options > kAwsMaxOptions || reserved != 0) return false;
  const uint8_t validMask = static_cast<uint8_t>((1u << options) - 1u);
  if (correct == 0 || correct == validMask || (correct & ~validMask) != 0) return false;

  uint32_t pos = 4;
  for (int i = 0; i < 2 + options; ++i) {
    if (pos >= length || pos > std::numeric_limits<uint16_t>::max()) return false;
    const char* start = out.bytes_ + pos;
    const void* end = std::memchr(start, '\0', length - pos);
    if (end == nullptr || end == start) return false;
    out.offset_[i] = static_cast<uint16_t>(pos);
    pos = static_cast<uint32_t>(static_cast<const char*>(end) - out.bytes_) + 1;
  }
  if (pos != length) return false;
  out.domain_ = domain;
  out.n_ = options;
  out.correct_ = correct;
  return true;
}

void AwsChooser::begin(const AwsPack& pack, PackState& state, Rng& rng) {
  pack_ = &pack;
  state_ = &state;
  rng_ = &rng;
}

bool AwsChooser::next(uint32_t& index, const uint8_t domains, const bool missedOnly) {
  if (!pack_ || !state_ || !rng_ || !pack_->count() || !(domains & kAwsAllDomains)) return false;
  const uint32_t count = pack_->count();
  const uint32_t start = rng_->below(count);
  // In regular mode prefer never-seen questions. Review mode is explicitly
  // restricted to missed questions regardless of the seen bit.
  for (int pass = 0; pass < (missedOnly ? 1 : 2); ++pass) {
    for (uint32_t step = 0; step < count; ++step) {
      const uint32_t i = (start + step) % count;
      const uint8_t flags = state_->flags(i);
      if (flags & kFlagged) continue;
      if (missedOnly && !(flags & kAwsMissed)) continue;
      if (!missedOnly && pass == 0 && (flags & kSeen)) continue;
      uint8_t domain = 0;
      if (!pack_->domainAt(i, domain)) continue;
      if ((domains & (1u << (domain - 1))) == 0) continue;
      index = i;
      return true;
    }
  }
  return false;
}

AwsTextPage awsTextPage(const char* text, uint32_t from, const uint32_t maxChars) {
  AwsTextPage p{};
  if (!text || !maxChars) return p;
  const uint32_t total = static_cast<uint32_t>(std::strlen(text));
  if (from >= total) return p;
  // Rewind if caller gave a continuation byte, so we never draw a torn UTF-8.
  while (from > 0 && (static_cast<uint8_t>(text[from]) & 0xc0u) == 0x80u) --from;
  p.start = from;
  uint32_t pos = from;
  uint32_t chars = 0;
  while (pos < total && chars < maxChars) {
    const uint8_t byte = static_cast<uint8_t>(text[pos]);
    uint32_t span = 1;
    if ((byte & 0xe0u) == 0xc0u) span = 2;
    else if ((byte & 0xf0u) == 0xe0u) span = 3;
    else if ((byte & 0xf8u) == 0xf0u) span = 4;
    if (pos + span > total) break;  // incomplete last codepoint
    pos += span;
    ++chars;
  }
  p.length = pos - from;
  p.more = pos < total;
  return p;
}

}  // namespace trivia
