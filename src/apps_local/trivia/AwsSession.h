#pragma once
// Pure C++17, 48-byte alternating-slot AWS session checkpoint.
// Never store question content, only a fingerprint and the active reader state.
#include <cstddef>
#include <cstdint>
namespace trivia {
constexpr size_t kAwsSessionBytes = 48;
constexpr uint8_t kAwsSessionQuestion = 1, kAwsSessionOption = 2;
constexpr uint8_t kAwsSessionExplanation = 3, kAwsSessionSummary = 4;
struct AwsSession {
  uint32_t fingerprint = 0, count = 0, current = 0, generation = 0;
  uint32_t asked = 0, right = 0;
  uint8_t view = 1, option = 0, selected = 0, page = 0;
  uint8_t review = 0, correct = 0, hasQuestion = 1, haveLast = 1;
  uint8_t summaryScroll = 0, domains = 0x3f;
};
inline void awsPut32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
inline uint32_t awsGet32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t awsSessionChecksum(const uint8_t* data) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < kAwsSessionBytes - 4; ++i) {
    hash ^= data[i]; hash *= 16777619u;
  }
  return hash;
}
inline bool awsSessionValid(const AwsSession& s, uint32_t fingerprint, uint32_t count) {
  if (!count || s.count != count || s.fingerprint != fingerprint ||
      s.current >= count || s.asked < s.right ||
      s.view < kAwsSessionQuestion || s.view > kAwsSessionSummary ||
      s.option >= 6 || (s.selected & 0xc0) != 0 || s.page >= 64 ||
      s.review > 1 || s.correct > 1 || s.hasQuestion > 1 ||
      s.haveLast != 1 || s.summaryScroll > 1 ||
      s.domains == 0 || (s.domains & ~0x3fu) != 0) return false;
  if (s.view != kAwsSessionSummary && !s.hasQuestion) return false;
  if (s.view == kAwsSessionExplanation && s.asked == 0) return false;
  return true;
}
inline void encodeAwsSession(const AwsSession& s, uint8_t (&out)[kAwsSessionBytes]) {
  for (size_t i = 0; i < kAwsSessionBytes; ++i) out[i] = 0;
  const uint8_t magic[] = {'A','W','S','S','E','S','S','1'};
  for (size_t i = 0; i < sizeof(magic); ++i) out[i] = magic[i];
  out[8] = 1;
  out[9] = s.view; out[10] = s.option; out[11] = s.selected; out[12] = s.page;
  out[13] = s.review; out[14] = s.correct; out[15] = s.hasQuestion;
  out[16] = s.haveLast; out[17] = s.summaryScroll; out[18] = s.domains;
  awsPut32(out + 20, s.fingerprint); awsPut32(out + 24, s.count);
  awsPut32(out + 28, s.current); awsPut32(out + 32, s.generation);
  awsPut32(out + 36, s.asked); awsPut32(out + 40, s.right);
  awsPut32(out + 44, awsSessionChecksum(out));
}
inline bool decodeAwsSession(const uint8_t* data, size_t len, uint32_t fingerprint,
                             uint32_t count, AwsSession& out) {
  const uint8_t magic[] = {'A','W','S','S','E','S','S','1'};
  if (!data || len != kAwsSessionBytes || data[8] != 1 || data[19] != 0) return false;
  for (size_t i = 0; i < sizeof(magic); ++i) if (data[i] != magic[i]) return false;
  if (awsGet32(data + 44) != awsSessionChecksum(data)) return false;
  AwsSession s;
  s.view = data[9]; s.option = data[10]; s.selected = data[11]; s.page = data[12];
  s.review = data[13]; s.correct = data[14]; s.hasQuestion = data[15];
  s.haveLast = data[16]; s.summaryScroll = data[17]; s.domains = data[18];
  s.fingerprint = awsGet32(data + 20); s.count = awsGet32(data + 24);
  s.current = awsGet32(data + 28); s.generation = awsGet32(data + 32);
  s.asked = awsGet32(data + 36); s.right = awsGet32(data + 40);
  if (!awsSessionValid(s, fingerprint, count)) return false;
  out = s;
  return true;
}
inline bool awsSessionNewer(uint32_t a, uint32_t b) {
  return static_cast<int32_t>(a - b) > 0;
}
}  // namespace trivia
