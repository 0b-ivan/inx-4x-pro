#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace passkey {

constexpr std::size_t kHidReportBytes = 64;
constexpr std::size_t kMaxMessageBytes = 1200;
constexpr uint32_t kBroadcastCid = 0xffffffffU;

struct HidMessage {
  uint32_t cid = 0;
  uint8_t command = 0;
  std::size_t length = 0;
  std::array<uint8_t, kMaxMessageBytes> payload{};
};

class CtapHidAssembler {
 public:
  bool ingest(const uint8_t* frame, std::size_t len, HidMessage& completed, uint8_t& errorCode);
  void reset();

 private:
  bool active_ = false;
  uint32_t cid_ = 0;
  uint8_t command_ = 0;
  uint16_t expected_ = 0;
  uint16_t received_ = 0;
  uint8_t nextSequence_ = 0;
  std::array<uint8_t, kMaxMessageBytes> payload_{};
};

class CtapProcessor {
 public:
  bool process(const HidMessage& request, HidMessage& response);

 private:
  uint32_t nextCid_ = 0x01020304U;
  uint32_t allocateCid();
  bool processInit(const HidMessage& request, HidMessage& response);
  bool processPing(const HidMessage& request, HidMessage& response) const;
  bool processCbor(const HidMessage& request, HidMessage& response) const;
  static void buildHidError(uint32_t cid, uint8_t code, HidMessage& response);
};

// Encodes one logical CTAP-HID message into one or more 64-byte reports.
// Returns the number of reports written, or 0 if the output buffer is too small.
std::size_t encodeReports(const HidMessage& message, uint8_t* out, std::size_t outBytes);

}  // namespace passkey
