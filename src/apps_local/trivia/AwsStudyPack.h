#pragma once

// AWS DevOps Professional study pack, kept separate from XTRIVIA v1.
// No heap and no index resident in RAM. A 150-question pack is read directly
// from SD; private purchased contents must never be committed to the repo.

#include <cstdint>
#include "TriviaCore.h"

namespace trivia {

inline constexpr uint8_t kAwsMagic[8] = {'X', 'A', 'W', 'S', 'D', 'E', 'V', 0};
inline constexpr uint16_t kAwsVersion = 1;
inline constexpr uint8_t kAwsDomains = 6;
inline constexpr uint8_t kAwsAllDomains = 0x3f;
inline constexpr uint8_t kAwsMaxOptions = 6;
// Measured private example: <9KiB including explanations. Larger records
// must be rejected, never truncated, and validated by the offline builder.
inline constexpr uint32_t kAwsMaxRecord = 12 * 1024;
inline constexpr uint8_t kAwsMissed = 0x04;

class AwsQuestion {
 public:
  uint8_t domain() const { return domain_; }
  uint8_t optionCount() const { return n_; }
  uint8_t correctMask() const { return correct_; }
  bool multiple() const { return (correct_ & (correct_ - 1u)) != 0; }
  const char* question() const { return text(0); }
  const char* explanation() const { return text(1); }
  const char* option(const uint8_t i) const {
    return i < n_ ? text(2 + i) : "";
  }
  bool evaluate(const uint8_t selectedMask) const { return selectedMask == correct_; }

 private:
  friend class AwsPack;
  const char* text(const int i) const {
    return i < 2 + n_ ? bytes_ + offset_[i] : "";
  }
  char bytes_[kAwsMaxRecord] = {};
  uint16_t offset_[2 + kAwsMaxOptions] = {};
  uint8_t n_ = 0;
  uint8_t domain_ = 0;
  uint8_t correct_ = 0;
};

class AwsPack {
 public:
  bool open(ByteSource& source);
  uint32_t count() const { return count_; }
  uint32_t fingerprint() const { return fingerprint_; }
  bool isOpen() const { return source_ != nullptr; }
  bool domainAt(uint32_t index, uint8_t& domain) const;
  bool read(uint32_t index, AwsQuestion& out) const;

 private:
  bool extent(uint32_t index, uint32_t& absolute, uint32_t& length) const;
  ByteSource* source_ = nullptr;
  uint32_t count_ = 0;
  uint32_t fingerprint_ = 0;
  uint32_t indexAt_ = 0;
  uint32_t base_ = 0;
};

class AwsChooser {
 public:
  void begin(const AwsPack& pack, PackState& state, Rng& rng);
  // Domain mask bit 0 = Domain 1. 0 is intentionally "no categories", not all.
  bool next(uint32_t& index, uint8_t enabledDomains, bool missedOnly = false);

 private:
  const AwsPack* pack_ = nullptr;
  PackState* state_ = nullptr;
  Rng* rng_ = nullptr;
};

// UTF-8 aware page window. Never split a codepoint. Character count is a
// deliberately conservative display bound, not a guaranteed pixel fit;
// rendering layouts must still provide adequate wrapped text height.
struct AwsTextPage {
  uint32_t start = 0;
  uint32_t length = 0;
  bool more = false;
};
AwsTextPage awsTextPage(const char* text, uint32_t fromByte, uint32_t maxCodepoints);

}  // namespace trivia
