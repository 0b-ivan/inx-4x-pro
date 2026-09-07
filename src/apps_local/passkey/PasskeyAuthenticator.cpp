#include "PasskeyAuthenticator.h"

#include <cstring>

namespace passkey {
namespace {

constexpr uint8_t kAuthenticatorFlagUp = 0x01;
constexpr std::size_t kSignedAssertionBytes = kAuthenticatorDataBytes + kSha256Bytes;

// The USB passkey worker serializes authenticator operations. Keep sensitive
// scratch storage here instead of combining a ~230-byte credential with crypto
// buffers on the FreeRTOS task stack.
PasskeyCredential g_credentialScratch{};
std::array<uint8_t, kSignedAssertionBytes> g_signedAssertionScratch{};
std::array<uint8_t, kSha256Bytes> g_digestScratch{};

void writeBe32(uint8_t* out, const uint32_t value) {
  out[0] = static_cast<uint8_t>(value >> 24U);
  out[1] = static_cast<uint8_t>(value >> 16U);
  out[2] = static_cast<uint8_t>(value >> 8U);
  out[3] = static_cast<uint8_t>(value);
}

void clearScratch() {
  secureZero(&g_credentialScratch, sizeof(g_credentialScratch));
  secureZero(g_signedAssertionScratch.data(), g_signedAssertionScratch.size());
  secureZero(g_digestScratch.data(), g_digestScratch.size());
}

}  // namespace

bool PasskeyAuthenticator::begin() { return credentialStore().begin(); }

bool PasskeyAuthenticator::createCredential(const uint8_t rpIdHash[kRpIdHashBytes], const uint8_t* userHandle,
                                             const std::size_t userHandleLength, PublicCredential& created) {
  created = PublicCredential{};
  clearScratch();

  if (rpIdHash == nullptr || (userHandle == nullptr && userHandleLength != 0) || userHandleLength > kMaxUserHandleBytes ||
      !begin()) {
    return false;
  }

  if (!cryptoRandom(g_credentialScratch.credentialId.data(), g_credentialScratch.credentialId.size()) ||
      !generateP256KeyPair(g_credentialScratch.keyPair)) {
    clearScratch();
    return false;
  }

  std::memcpy(g_credentialScratch.rpIdHash.data(), rpIdHash, g_credentialScratch.rpIdHash.size());
  g_credentialScratch.userHandleLength = static_cast<uint8_t>(userHandleLength);
  if (userHandleLength > 0) {
    std::memcpy(g_credentialScratch.userHandle.data(), userHandle, userHandleLength);
  }
  g_credentialScratch.signCount = 0;

  if (!credentialStore().save(g_credentialScratch)) {
    clearScratch();
    return false;
  }

  created.credentialId = g_credentialScratch.credentialId;
  created.publicX = g_credentialScratch.keyPair.publicX;
  created.publicY = g_credentialScratch.keyPair.publicY;
  clearScratch();
  return true;
}

bool PasskeyAuthenticator::getAssertion(const uint8_t* credentialId, const std::size_t credentialIdLength,
                                         const uint8_t rpIdHash[kRpIdHashBytes],
                                         const uint8_t clientDataHash[kSha256Bytes], AssertionResult& assertion) {
  assertion = AssertionResult{};
  clearScratch();

  if (credentialId == nullptr || credentialIdLength != kCredentialIdBytes || rpIdHash == nullptr ||
      clientDataHash == nullptr || !begin()) {
    return false;
  }

  if (!credentialStore().findByCredentialId(credentialId, credentialIdLength, g_credentialScratch) ||
      std::memcmp(g_credentialScratch.rpIdHash.data(), rpIdHash, g_credentialScratch.rpIdHash.size()) != 0) {
    clearScratch();
    return false;
  }

  uint32_t signCount = 0;
  if (!credentialStore().incrementSignCount(credentialId, credentialIdLength, signCount)) {
    clearScratch();
    return false;
  }

  std::memcpy(assertion.authenticatorData.data(), rpIdHash, kRpIdHashBytes);
  assertion.authenticatorData[kRpIdHashBytes] = kAuthenticatorFlagUp;
  writeBe32(assertion.authenticatorData.data() + kRpIdHashBytes + 1, signCount);

  std::memcpy(g_signedAssertionScratch.data(), assertion.authenticatorData.data(), assertion.authenticatorData.size());
  std::memcpy(g_signedAssertionScratch.data() + assertion.authenticatorData.size(), clientDataHash, kSha256Bytes);
  if (!sha256(g_signedAssertionScratch.data(), g_signedAssertionScratch.size(), g_digestScratch.data()) ||
      !signP256Sha256(g_credentialScratch.keyPair, g_digestScratch.data(), assertion.signature.data(),
                      assertion.signature.size(), assertion.signatureLength)) {
    assertion = AssertionResult{};
    clearScratch();
    return false;
  }

  assertion.credentialId = g_credentialScratch.credentialId;
  assertion.userHandleLength = g_credentialScratch.userHandleLength;
  if (assertion.userHandleLength > 0) {
    std::memcpy(assertion.userHandle.data(), g_credentialScratch.userHandle.data(), assertion.userHandleLength);
  }

  clearScratch();
  return true;
}

PasskeyAuthenticator& authenticator() {
  static PasskeyAuthenticator instance;
  return instance;
}

}  // namespace passkey
