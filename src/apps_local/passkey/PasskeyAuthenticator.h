#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PasskeyCrypto.h"
#include "PasskeyStore.h"

namespace passkey {

constexpr std::size_t kAuthenticatorDataBytes = kRpIdHashBytes + 1 + 4;

struct PublicCredential {
  std::array<uint8_t, kCredentialIdBytes> credentialId{};
  std::array<uint8_t, kP256CoordinateBytes> publicX{};
  std::array<uint8_t, kP256CoordinateBytes> publicY{};
};

struct AssertionResult {
  std::array<uint8_t, kCredentialIdBytes> credentialId{};
  uint8_t userHandleLength = 0;
  std::array<uint8_t, kMaxUserHandleBytes> userHandle{};
  std::array<uint8_t, kAuthenticatorDataBytes> authenticatorData{};
  std::array<uint8_t, kMaxEs256DerSignatureBytes> signature{};
  std::size_t signatureLength = 0;
};

class PasskeyAuthenticator {
 public:
  bool begin();

  // Creates and persists a credential while only exposing its public half to
  // the CTAP layer. The private scalar never leaves this module/store boundary.
  bool createCredential(const uint8_t rpIdHash[kRpIdHashBytes], const uint8_t* userHandle,
                        std::size_t userHandleLength, PublicCredential& created);

  // Builds authenticatorData with UP=true, advances the persistent signature
  // counter, hashes authenticatorData || clientDataHash, and returns an ES256
  // signature. UV remains false until a separate verification path exists.
  bool getAssertion(const uint8_t* credentialId, std::size_t credentialIdLength,
                    const uint8_t rpIdHash[kRpIdHashBytes], const uint8_t clientDataHash[kSha256Bytes],
                    AssertionResult& assertion);
};

PasskeyAuthenticator& authenticator();

}  // namespace passkey
