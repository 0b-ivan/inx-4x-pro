#include "PasskeyCtap.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "PasskeyAuthenticator.h"
#include "PasskeyCbor.h"
#include "PasskeyCrypto.h"
#include "PasskeyPresence.h"
#include "PasskeyStore.h"

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
#include <Arduino.h>
#endif

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

constexpr uint8_t kCtapMakeCredential = 0x01;
constexpr uint8_t kCtapGetAssertion = 0x02;
constexpr uint8_t kCtapGetInfo = 0x04;
constexpr uint8_t kCtapOk = 0x00;
constexpr uint8_t kCtapErrInvalidCommand = 0x01;
constexpr uint8_t kCtapErrInvalidCbor = 0x12;
constexpr uint8_t kCtapErrMissingParameter = 0x14;
constexpr uint8_t kCtapErrLimitExceeded = 0x15;
constexpr uint8_t kCtapErrCredentialExcluded = 0x19;
constexpr uint8_t kCtapErrUnsupportedAlgorithm = 0x26;
constexpr uint8_t kCtapErrOperationDenied = 0x27;
constexpr uint8_t kCtapErrKeyStoreFull = 0x28;
constexpr uint8_t kCtapErrUnsupportedOption = 0x2b;
constexpr uint8_t kCtapErrNoCredentials = 0x2e;
constexpr uint8_t kCtapErrActionTimeout = 0x3a;
constexpr uint8_t kCtapErrOther = 0x7f;

constexpr std::size_t kMaxListCredentials = 8;
constexpr std::size_t kMaxRpIdBytes = kMaxPresenceRpIdBytes;
constexpr unsigned kPresenceTimeoutMs = 30000;
constexpr unsigned kKeepAliveEveryMs = 250;

constexpr uint8_t kAuthenticatorFlagUp = 0x01;
constexpr uint8_t kAuthenticatorFlagAt = 0x40;

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

bool textEquals(const char* value, const std::size_t length, const char* expected) {
  const std::size_t expectedLength = std::strlen(expected);
  return value != nullptr && length == expectedLength && std::memcmp(value, expected, length) == 0;
}

bool copyText(const char* value, const std::size_t length, char* out, const std::size_t capacity) {
  if (value == nullptr || out == nullptr || length == 0 || length >= capacity) return false;
  std::memcpy(out, value, length);
  out[length] = '\0';
  return true;
}

enum class ParseResult : uint8_t { Ok, InvalidCbor, MissingParameter, LimitExceeded, UnsupportedAlgorithm, UnsupportedOption };

struct MakeCredentialRequest {
  std::array<uint8_t, kSha256Bytes> clientDataHash{};
  std::array<char, kMaxRpIdBytes + 1> rpId{};
  std::size_t rpIdLength = 0;
  std::array<uint8_t, kMaxUserHandleBytes> userHandle{};
  std::size_t userHandleLength = 0;
  std::array<std::array<uint8_t, kCredentialIdBytes>, kMaxListCredentials> excludeIds{};
  std::size_t excludeCount = 0;
  bool supportsEs256 = false;
};

struct GetAssertionRequest {
  std::array<char, kMaxRpIdBytes + 1> rpId{};
  std::size_t rpIdLength = 0;
  std::array<uint8_t, kSha256Bytes> clientDataHash{};
  std::array<std::array<uint8_t, kCredentialIdBytes>, kMaxListCredentials> allowIds{};
  std::size_t allowCount = 0;
};

MakeCredentialRequest g_makeRequest{};
GetAssertionRequest g_assertRequest{};
std::array<uint8_t, kRpIdHashBytes> g_rpIdHash{};
std::array<uint8_t, 192> g_authData{};
PublicCredential g_publicCredential{};
AssertionResult g_assertion{};

ParseResult parseRp(CborReader& reader, MakeCredentialRequest& request) {
  std::size_t count = 0;
  if (!reader.readMap(count)) return ParseResult::InvalidCbor;
  bool hasId = false;
  for (std::size_t i = 0; i < count; ++i) {
    const char* key = nullptr;
    std::size_t keyLength = 0;
    if (!reader.readText(key, keyLength)) return ParseResult::InvalidCbor;
    if (textEquals(key, keyLength, "id")) {
      const char* id = nullptr;
      std::size_t idLength = 0;
      if (!reader.readText(id, idLength) || idLength > kMaxRpIdBytes ||
          !copyText(id, idLength, request.rpId.data(), request.rpId.size())) {
        return ParseResult::InvalidCbor;
      }
      request.rpIdLength = idLength;
      hasId = true;
    } else if (!reader.skip()) {
      return ParseResult::InvalidCbor;
    }
  }
  return hasId ? ParseResult::Ok : ParseResult::MissingParameter;
}

ParseResult parseUser(CborReader& reader, MakeCredentialRequest& request) {
  std::size_t count = 0;
  if (!reader.readMap(count)) return ParseResult::InvalidCbor;
  bool hasId = false;
  for (std::size_t i = 0; i < count; ++i) {
    const char* key = nullptr;
    std::size_t keyLength = 0;
    if (!reader.readText(key, keyLength)) return ParseResult::InvalidCbor;
    if (textEquals(key, keyLength, "id")) {
      const uint8_t* id = nullptr;
      std::size_t idLength = 0;
      if (!reader.readBytes(id, idLength) || idLength > kMaxUserHandleBytes) return ParseResult::InvalidCbor;
      if (idLength > 0) std::memcpy(request.userHandle.data(), id, idLength);
      request.userHandleLength = idLength;
      hasId = true;
    } else if (!reader.skip()) {
      return ParseResult::InvalidCbor;
    }
  }
  return hasId ? ParseResult::Ok : ParseResult::MissingParameter;
}

ParseResult parseAlgorithms(CborReader& reader, MakeCredentialRequest& request) {
  std::size_t count = 0;
  if (!reader.readArray(count)) return ParseResult::InvalidCbor;
  if (count == 0 || count > 16) return ParseResult::LimitExceeded;

  for (std::size_t i = 0; i < count; ++i) {
    std::size_t fields = 0;
    if (!reader.readMap(fields)) return ParseResult::InvalidCbor;
    bool publicKey = false;
    bool hasAlg = false;
    int64_t alg = 0;
    for (std::size_t field = 0; field < fields; ++field) {
      const char* key = nullptr;
      std::size_t keyLength = 0;
      if (!reader.readText(key, keyLength)) return ParseResult::InvalidCbor;
      if (textEquals(key, keyLength, "type")) {
        const char* value = nullptr;
        std::size_t valueLength = 0;
        if (!reader.readText(value, valueLength)) return ParseResult::InvalidCbor;
        publicKey = textEquals(value, valueLength, "public-key");
      } else if (textEquals(key, keyLength, "alg")) {
        if (!reader.readSigned(alg)) return ParseResult::InvalidCbor;
        hasAlg = true;
      } else if (!reader.skip()) {
        return ParseResult::InvalidCbor;
      }
    }
    if (publicKey && hasAlg && alg == -7) request.supportsEs256 = true;
  }
  return request.supportsEs256 ? ParseResult::Ok : ParseResult::UnsupportedAlgorithm;
}

ParseResult parseCredentialList(CborReader& reader,
                                std::array<std::array<uint8_t, kCredentialIdBytes>, kMaxListCredentials>& ids,
                                std::size_t& idCount) {
  std::size_t count = 0;
  if (!reader.readArray(count)) return ParseResult::InvalidCbor;
  if (count > kMaxListCredentials) return ParseResult::LimitExceeded;
  idCount = 0;

  for (std::size_t i = 0; i < count; ++i) {
    std::size_t fields = 0;
    if (!reader.readMap(fields)) return ParseResult::InvalidCbor;
    bool publicKey = true;
    bool hasId = false;
    std::array<uint8_t, kCredentialIdBytes> candidate{};
    for (std::size_t field = 0; field < fields; ++field) {
      const char* key = nullptr;
      std::size_t keyLength = 0;
      if (!reader.readText(key, keyLength)) return ParseResult::InvalidCbor;
      if (textEquals(key, keyLength, "id")) {
        const uint8_t* id = nullptr;
        std::size_t idLength = 0;
        if (!reader.readBytes(id, idLength)) return ParseResult::InvalidCbor;
        if (idLength == kCredentialIdBytes) {
          std::memcpy(candidate.data(), id, idLength);
          hasId = true;
        }
      } else if (textEquals(key, keyLength, "type")) {
        const char* value = nullptr;
        std::size_t valueLength = 0;
        if (!reader.readText(value, valueLength)) return ParseResult::InvalidCbor;
        publicKey = textEquals(value, valueLength, "public-key");
      } else if (!reader.skip()) {
        return ParseResult::InvalidCbor;
      }
    }
    if (hasId && publicKey && idCount < ids.size()) ids[idCount++] = candidate;
  }
  return ParseResult::Ok;
}

ParseResult parseOptions(CborReader& reader) {
  std::size_t count = 0;
  if (!reader.readMap(count)) return ParseResult::InvalidCbor;
  for (std::size_t i = 0; i < count; ++i) {
    const char* key = nullptr;
    std::size_t keyLength = 0;
    if (!reader.readText(key, keyLength)) return ParseResult::InvalidCbor;
    if (textEquals(key, keyLength, "rk") || textEquals(key, keyLength, "uv")) {
      bool enabled = false;
      if (!reader.readBool(enabled)) return ParseResult::InvalidCbor;
      if (enabled) return ParseResult::UnsupportedOption;
    } else if (textEquals(key, keyLength, "up")) {
      bool enabled = false;
      if (!reader.readBool(enabled)) return ParseResult::InvalidCbor;
      if (!enabled) return ParseResult::UnsupportedOption;
    } else if (!reader.skip()) {
      return ParseResult::InvalidCbor;
    }
  }
  return ParseResult::Ok;
}

ParseResult parseMakeCredential(const uint8_t* data, const std::size_t size, MakeCredentialRequest& request) {
  request = MakeCredentialRequest{};
  CborReader reader(data, size);
  std::size_t count = 0;
  if (!reader.readMap(count)) return ParseResult::InvalidCbor;
  bool hasClientData = false;
  bool hasRp = false;
  bool hasUser = false;
  bool hasParams = false;

  for (std::size_t i = 0; i < count; ++i) {
    uint64_t key = 0;
    if (!reader.readUnsigned(key)) return ParseResult::InvalidCbor;
    ParseResult result = ParseResult::Ok;
    switch (key) {
      case 1: {
        const uint8_t* bytes = nullptr;
        std::size_t length = 0;
        if (!reader.readBytes(bytes, length) || length != kSha256Bytes) return ParseResult::InvalidCbor;
        std::memcpy(request.clientDataHash.data(), bytes, length);
        hasClientData = true;
        break;
      }
      case 2:
        result = parseRp(reader, request);
        hasRp = result == ParseResult::Ok;
        break;
      case 3:
        result = parseUser(reader, request);
        hasUser = result == ParseResult::Ok;
        break;
      case 4:
        result = parseAlgorithms(reader, request);
        hasParams = result == ParseResult::Ok;
        break;
      case 5:
        result = parseCredentialList(reader, request.excludeIds, request.excludeCount);
        break;
      case 7:
        result = parseOptions(reader);
        break;
      default:
        if (!reader.skip()) result = ParseResult::InvalidCbor;
        break;
    }
    if (result != ParseResult::Ok) return result;
  }
  if (!reader.finished()) return ParseResult::InvalidCbor;
  return hasClientData && hasRp && hasUser && hasParams ? ParseResult::Ok : ParseResult::MissingParameter;
}

ParseResult parseGetAssertion(const uint8_t* data, const std::size_t size, GetAssertionRequest& request) {
  request = GetAssertionRequest{};
  CborReader reader(data, size);
  std::size_t count = 0;
  if (!reader.readMap(count)) return ParseResult::InvalidCbor;
  bool hasRp = false;
  bool hasClientData = false;

  for (std::size_t i = 0; i < count; ++i) {
    uint64_t key = 0;
    if (!reader.readUnsigned(key)) return ParseResult::InvalidCbor;
    ParseResult result = ParseResult::Ok;
    switch (key) {
      case 1: {
        const char* rp = nullptr;
        std::size_t length = 0;
        if (!reader.readText(rp, length) || length > kMaxRpIdBytes ||
            !copyText(rp, length, request.rpId.data(), request.rpId.size())) {
          return ParseResult::InvalidCbor;
        }
        request.rpIdLength = length;
        hasRp = true;
        break;
      }
      case 2: {
        const uint8_t* bytes = nullptr;
        std::size_t length = 0;
        if (!reader.readBytes(bytes, length) || length != kSha256Bytes) return ParseResult::InvalidCbor;
        std::memcpy(request.clientDataHash.data(), bytes, length);
        hasClientData = true;
        break;
      }
      case 3:
        result = parseCredentialList(reader, request.allowIds, request.allowCount);
        break;
      case 5:
        result = parseOptions(reader);
        break;
      default:
        if (!reader.skip()) result = ParseResult::InvalidCbor;
        break;
    }
    if (result != ParseResult::Ok) return result;
  }
  if (!reader.finished()) return ParseResult::InvalidCbor;
  return hasRp && hasClientData ? ParseResult::Ok : ParseResult::MissingParameter;
}

uint8_t parseError(const ParseResult result) {
  switch (result) {
    case ParseResult::MissingParameter:
      return kCtapErrMissingParameter;
    case ParseResult::LimitExceeded:
      return kCtapErrLimitExceeded;
    case ParseResult::UnsupportedAlgorithm:
      return kCtapErrUnsupportedAlgorithm;
    case ParseResult::UnsupportedOption:
      return kCtapErrUnsupportedOption;
    case ParseResult::InvalidCbor:
      return kCtapErrInvalidCbor;
    case ParseResult::Ok:
      return kCtapOk;
  }
  return kCtapErrInvalidCbor;
}

void ctapError(const HidMessage& request, const uint8_t code, HidMessage& response) {
  response = HidMessage{};
  response.cid = request.cid;
  response.command = kCmdCbor;
  response.length = 1;
  response.payload[0] = code;
}

enum class PresenceWait : uint8_t { Approved, Denied, Timeout, Busy };

PresenceWait waitForPresence(const PresenceAction action, const char* rpId, const std::size_t rpIdLength,
                             const uint32_t cid, CtapProcessor::KeepAliveSender keepAlive) {
  if (!presence().request(action, rpId, rpIdLength)) return PresenceWait::Busy;

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
  const unsigned long started = millis();
  unsigned long lastKeepAlive = 0;
  while (millis() - started < kPresenceTimeoutMs) {
    const PresenceDecision decision = presence().decision();
    if (decision == PresenceDecision::Approved) return PresenceWait::Approved;
    if (decision == PresenceDecision::Denied) {
      presence().clear();
      return PresenceWait::Denied;
    }
    const unsigned long now = millis();
    if (keepAlive != nullptr && (lastKeepAlive == 0 || now - lastKeepAlive >= kKeepAliveEveryMs)) {
      keepAlive(cid);
      lastKeepAlive = now;
    }
    delay(10);
  }
  presence().clear();
  return PresenceWait::Timeout;
#else
  (void)cid;
  (void)keepAlive;
  presence().clear();
  return PresenceWait::Timeout;
#endif
}

uint8_t presenceError(const PresenceWait result) {
  if (result == PresenceWait::Denied || result == PresenceWait::Busy) return kCtapErrOperationDenied;
  if (result == PresenceWait::Timeout) return kCtapErrActionTimeout;
  return kCtapOk;
}

bool buildMakeCredentialResponse(const PublicCredential& credential, const uint8_t rpIdHash[kRpIdHashBytes],
                                 HidMessage& response) {
  g_authData.fill(0);
  std::size_t offset = 0;
  std::memcpy(g_authData.data() + offset, rpIdHash, kRpIdHashBytes);
  offset += kRpIdHashBytes;
  g_authData[offset++] = static_cast<uint8_t>(kAuthenticatorFlagUp | kAuthenticatorFlagAt);
  writeBe32(g_authData.data() + offset, 0);
  offset += 4;
  offset += 16;  // development AAGUID: all zeros
  g_authData[offset++] = 0;
  g_authData[offset++] = static_cast<uint8_t>(kCredentialIdBytes);
  std::memcpy(g_authData.data() + offset, credential.credentialId.data(), credential.credentialId.size());
  offset += credential.credentialId.size();

  CborWriter cose(g_authData.data() + offset, g_authData.size() - offset);
  if (!cose.putMap(5) || !cose.putUnsigned(1) || !cose.putUnsigned(2) ||
      !cose.putUnsigned(3) || !cose.putSigned(-7) ||
      !cose.putSigned(-1) || !cose.putUnsigned(1) ||
      !cose.putSigned(-2) || !cose.putBytes(credential.publicX.data(), credential.publicX.size()) ||
      !cose.putSigned(-3) || !cose.putBytes(credential.publicY.data(), credential.publicY.size())) {
    return false;
  }
  offset += cose.size();

  response.payload[0] = kCtapOk;
  CborWriter writer(response.payload.data() + 1, response.payload.size() - 1);
  if (!writer.putMap(3) || !writer.putUnsigned(1) || !writer.putText("none", 4) ||
      !writer.putUnsigned(2) || !writer.putBytes(g_authData.data(), offset) ||
      !writer.putUnsigned(3) || !writer.putMap(0)) {
    return false;
  }
  response.length = 1 + writer.size();
  return true;
}

bool buildAssertionResponse(const AssertionResult& assertion, HidMessage& response) {
  response.payload[0] = kCtapOk;
  CborWriter writer(response.payload.data() + 1, response.payload.size() - 1);
  if (!writer.putMap(3) ||
      !writer.putUnsigned(1) || !writer.putMap(2) ||
      !writer.putText("id", 2) || !writer.putBytes(assertion.credentialId.data(), assertion.credentialId.size()) ||
      !writer.putText("type", 4) || !writer.putText("public-key", 10) ||
      !writer.putUnsigned(2) || !writer.putBytes(assertion.authenticatorData.data(), assertion.authenticatorData.size()) ||
      !writer.putUnsigned(3) || !writer.putBytes(assertion.signature.data(), assertion.signatureLength)) {
    return false;
  }
  response.length = 1 + writer.size();
  return true;
}

constexpr uint8_t kGetInfoResponse[] = {
    kCtapOk,
    0xa5,
    0x01, 0x81, 0x68, 'F', 'I', 'D', 'O', '_', '2', '_', '0',
    0x03, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0xa3,
    0x62, 'r', 'k', 0xf4,
    0x62, 'u', 'p', 0xf5,
    0x62, 'u', 'v', 0xf4,
    0x05, 0x19, 0x04, 0xb0,
    0x0a, 0x81, 0xa2,
    0x63, 'a', 'l', 'g', 0x26,
    0x64, 't', 'y', 'p', 'e', 0x6a, 'p', 'u', 'b', 'l', 'i', 'c', '-', 'k', 'e', 'y'
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
  response.payload[12] = 2;
  response.payload[13] = 1;
  response.payload[14] = 1;
  response.payload[15] = 0;
  response.payload[16] = 0x04;
  return true;
}

bool CtapProcessor::processPing(const HidMessage& request, HidMessage& response) const {
  response = request;
  return true;
}

bool CtapProcessor::processCbor(const HidMessage& request, HidMessage& response) {
  response = HidMessage{};
  response.cid = request.cid;
  response.command = kCmdCbor;
  if (request.length == 0) {
    ctapError(request, kCtapErrInvalidCommand, response);
    return true;
  }

  const uint8_t command = request.payload[0];
  if (command == kCtapGetInfo) {
    if (request.length != 1) {
      ctapError(request, kCtapErrInvalidCbor, response);
      return true;
    }
    response.length = sizeof(kGetInfoResponse);
    std::memcpy(response.payload.data(), kGetInfoResponse, sizeof(kGetInfoResponse));
    return true;
  }

  if (command == kCtapMakeCredential) {
    const ParseResult parsed = parseMakeCredential(request.payload.data() + 1, request.length - 1, g_makeRequest);
    if (parsed != ParseResult::Ok) {
      ctapError(request, parseError(parsed), response);
      return true;
    }
    if (!sha256(reinterpret_cast<const uint8_t*>(g_makeRequest.rpId.data()), g_makeRequest.rpIdLength,
                g_rpIdHash.data())) {
      ctapError(request, kCtapErrOther, response);
      return true;
    }
    for (std::size_t i = 0; i < g_makeRequest.excludeCount; ++i) {
      if (authenticator().hasCredential(g_makeRequest.excludeIds[i].data(), kCredentialIdBytes, g_rpIdHash.data())) {
        ctapError(request, kCtapErrCredentialExcluded, response);
        return true;
      }
    }
    if (credentialStore().credentialCount() >= kMaxStoredCredentials) {
      ctapError(request, kCtapErrKeyStoreFull, response);
      return true;
    }
    const PresenceWait allowed = waitForPresence(PresenceAction::CreateCredential, g_makeRequest.rpId.data(),
                                                 g_makeRequest.rpIdLength, request.cid, keepAliveSender_);
    if (allowed != PresenceWait::Approved) {
      ctapError(request, presenceError(allowed), response);
      return true;
    }
    if (!authenticator().createCredential(g_rpIdHash.data(), g_makeRequest.userHandle.data(),
                                          g_makeRequest.userHandleLength, g_publicCredential) ||
        !buildMakeCredentialResponse(g_publicCredential, g_rpIdHash.data(), response)) {
      presence().clear();
      ctapError(request, kCtapErrOther, response);
    }
    return true;
  }

  if (command == kCtapGetAssertion) {
    const ParseResult parsed = parseGetAssertion(request.payload.data() + 1, request.length - 1, g_assertRequest);
    if (parsed != ParseResult::Ok) {
      ctapError(request, parseError(parsed), response);
      return true;
    }
    if (g_assertRequest.allowCount == 0) {
      ctapError(request, kCtapErrNoCredentials, response);
      return true;
    }
    if (!sha256(reinterpret_cast<const uint8_t*>(g_assertRequest.rpId.data()), g_assertRequest.rpIdLength,
                g_rpIdHash.data())) {
      ctapError(request, kCtapErrOther, response);
      return true;
    }
    const uint8_t* selected = nullptr;
    for (std::size_t i = 0; i < g_assertRequest.allowCount; ++i) {
      if (authenticator().hasCredential(g_assertRequest.allowIds[i].data(), kCredentialIdBytes, g_rpIdHash.data())) {
        selected = g_assertRequest.allowIds[i].data();
        break;
      }
    }
    if (selected == nullptr) {
      ctapError(request, kCtapErrNoCredentials, response);
      return true;
    }
    const PresenceWait allowed = waitForPresence(PresenceAction::GetAssertion, g_assertRequest.rpId.data(),
                                                 g_assertRequest.rpIdLength, request.cid, keepAliveSender_);
    if (allowed != PresenceWait::Approved) {
      ctapError(request, presenceError(allowed), response);
      return true;
    }
    if (!authenticator().getAssertion(selected, kCredentialIdBytes, g_rpIdHash.data(),
                                      g_assertRequest.clientDataHash.data(), g_assertion) ||
        !buildAssertionResponse(g_assertion, response)) {
      presence().clear();
      ctapError(request, kCtapErrOther, response);
    }
    return true;
  }

  ctapError(request, kCtapErrInvalidCommand, response);
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
