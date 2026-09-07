#include "PasskeyCtap.h"

#include <algorithm>
#include <cstring>

namespace passkey {
namespace {

constexpr uint8_t kCmdPing = 0x01;
constexpr uint8_t kCmdInit = 0x06;
constexpr uint8_t kCmdCbor = 0x10;
constexpr uint8_t kCmdError = 0x3f;

constexpr uint8_t kErrInvalidCmd = 0x01;
constexpr uint8_t kErrInvalidLen = 0x03;
constexpr uint8_t kErrInvalidSeq = 0x04;
constexpr uint8_t kErrChannelBusy = 0x06;

constexpr uint8_t kCtapGetInfo = 0x04;
constexpr uint8_t kCtapOk = 0x00;
constexpr uint8_t kCtapErrInvalidCommand = 0x01;

uint32_t readBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24U) | (static_cast<uint32_t>(p[1]) << 16U) |
         (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
}

void writeBe32(uint8_t* p, const uint32_t value) {
  p[0] = static_cast<uint8_t>(value >> 24U);
  p[1] = static_cast<uint8_t>(value >> 16U);
  p[2] = static_cast<uint8_t>(value >> 8U);
  p[3] = static_cast<uint8_t>(value);
}

// Minimal authenticatorGetInfo response. This is intentionally conservative:
// FIDO_2_0 + CBOR transport are advertised, but resident keys, PIN/UV and
// credential creation are not claimed until those paths are implemented.
constexpr uint8_t kGetInfoResponse[] = {
    kCtapOk,
    0xa4,                    // map(4)
    0x01, 0x81, 0x68,       // 1: [text(8)
    'F', 'I', 'D', 'O', '_', '2', '_', '0',
    0x03, 0x50,              // 3: aaguid bytes(16)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0xa3,              // 4: options map(3)
    0x62, 'r', 'k', 0xf4,   // rk: false
    0x62, 'u', 'p', 0xf5,   // up: true
    0x62, 'u', 'v', 0xf4,   // uv: false
    0x05, 0x19, 0x04, 0xb0  // 5: maxMsgSize = 1200
};

}  // namespace

void CtapHidAssembler::reset() {
  active_ = false;
  cid_ = 0;
  command_ = 0;
  expected_ = 0;
  received_ = 0;
  nextSequence_ = 0;
}

bool CtapHidAssembler::ingest(const uint8_t* frame, const std::size_t len, HidMessage& completed,
                              uint8_t& errorCode) {
  errorCode = 0;
  if (frame == nullptr || len != kHidReportBytes) {
    errorCode = kErrInvalidLen;
    return false;
  }

  const uint32_t cid = readBe32(frame);
  const uint8_t marker = frame[4];

  if ((marker & 0x80U) != 0) {
    if (active_) {
      errorCode = kErrChannelBusy;
      return false;
    }

    const uint16_t declared = static_cast<uint16_t>((static_cast<uint16_t>(frame[5]) << 8U) | frame[6]);
    if (declared > kMaxMessageBytes) {
      errorCode = kErrInvalidLen;
      return false;
    }

    active_ = true;
    cid_ = cid;
    command_ = static_cast<uint8_t>(marker & 0x7fU);
    expected_ = declared;
    received_ = 0;
    nextSequence_ = 0;

    const std::size_t chunk = std::min<std::size_t>(expected_, 57U);
    if (chunk > 0) std::memcpy(payload_.data(), frame + 7, chunk);
    received_ = static_cast<uint16_t>(chunk);
  } else {
    if (!active_ || cid != cid_ || marker != nextSequence_) {
      errorCode = kErrInvalidSeq;
      reset();
      return false;
    }

    ++nextSequence_;
    const std::size_t remaining = static_cast<std::size_t>(expected_ - received_);
    const std::size_t chunk = std::min<std::size_t>(remaining, 59U);
    if (chunk > 0) std::memcpy(payload_.data() + received_, frame + 5, chunk);
    received_ = static_cast<uint16_t>(received_ + chunk);
  }

  if (received_ < expected_) return false;

  completed = HidMessage{};
  completed.cid = cid_;
  completed.command = command_;
  completed.length = expected_;
  if (expected_ > 0) std::memcpy(completed.payload.data(), payload_.data(), expected_);
  reset();
  return true;
}

uint32_t CtapProcessor::allocateCid() {
  ++nextCid_;
  if (nextCid_ == 0 || nextCid_ == kBroadcastCid) nextCid_ = 0x01020304U;
  return nextCid_;
}

void CtapProcessor::buildHidError(const uint32_t cid, const uint8_t code, HidMessage& response) {
  response = HidMessage{};
  response.cid = cid;
  response.command = kCmdError;
  response.length = 1;
  response.payload[0] = code;
}

bool CtapProcessor::processInit(const HidMessage& request, HidMessage& response) {
  if (request.length != 8) {
    buildHidError(request.cid, kErrInvalidLen, response);
    return true;
  }

  const uint32_t assigned = request.cid == kBroadcastCid ? allocateCid() : request.cid;
  response = HidMessage{};
  response.cid = request.cid;
  response.command = kCmdInit;
  response.length = 17;
  std::memcpy(response.payload.data(), request.payload.data(), 8);
  writeBe32(response.payload.data() + 8, assigned);
  response.payload[12] = 2;     // CTAPHID protocol version
  response.payload[13] = 1;     // device major
  response.payload[14] = 0;     // device minor
  response.payload[15] = 0;     // device build
  response.payload[16] = 0x04;  // CAPABILITY_CBOR
  return true;
}

bool CtapProcessor::processPing(const HidMessage& request, HidMessage& response) const {
  response = request;
  return true;
}

bool CtapProcessor::processCbor(const HidMessage& request, HidMessage& response) const {
  response = HidMessage{};
  response.cid = request.cid;
  response.command = kCmdCbor;

  if (request.length == 1 && request.payload[0] == kCtapGetInfo) {
    response.length = sizeof(kGetInfoResponse);
    std::memcpy(response.payload.data(), kGetInfoResponse, sizeof(kGetInfoResponse));
    return true;
  }

  response.length = 1;
  response.payload[0] = kCtapErrInvalidCommand;
  return true;
}

bool CtapProcessor::process(const HidMessage& request, HidMessage& response) {
  switch (request.command) {
    case kCmdInit:
      return processInit(request, response);
    case kCmdPing:
      return processPing(request, response);
    case kCmdCbor:
      return processCbor(request, response);
    default:
      buildHidError(request.cid, kErrInvalidCmd, response);
      return true;
  }
}

std::size_t encodeReports(const HidMessage& message, uint8_t* out, const std::size_t outBytes) {
  if (out == nullptr || message.length > kMaxMessageBytes) return 0;
  const std::size_t continuationBytes = message.length > 57U ? message.length - 57U : 0U;
  const std::size_t continuationReports = (continuationBytes + 58U) / 59U;
  const std::size_t reports = 1U + continuationReports;
  if (outBytes < reports * kHidReportBytes) return 0;

  std::memset(out, 0, reports * kHidReportBytes);
  writeBe32(out, message.cid);
  out[4] = static_cast<uint8_t>(message.command | 0x80U);
  out[5] = static_cast<uint8_t>(message.length >> 8U);
  out[6] = static_cast<uint8_t>(message.length);

  std::size_t copied = std::min<std::size_t>(message.length, 57U);
  if (copied > 0) std::memcpy(out + 7, message.payload.data(), copied);

  for (std::size_t report = 1; report < reports; ++report) {
    uint8_t* frame = out + report * kHidReportBytes;
    writeBe32(frame, message.cid);
    frame[4] = static_cast<uint8_t>(report - 1U);
    const std::size_t remaining = message.length - copied;
    const std::size_t chunk = std::min<std::size_t>(remaining, 59U);
    if (chunk > 0) std::memcpy(frame + 5, message.payload.data() + copied, chunk);
    copied += chunk;
  }

  return reports;
}

}  // namespace passkey
