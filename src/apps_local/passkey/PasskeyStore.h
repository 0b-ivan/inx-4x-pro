#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PasskeyCrypto.h"

namespace passkey {

constexpr std::size_t kCredentialIdBytes = 32;
constexpr std::size_t kRpIdHashBytes = kSha256Bytes;
constexpr std::size_t kMaxUserHandleBytes = 64;
constexpr std::size_t kMaxStoredCredentials = 16;

struct PasskeyCredential {
  std::array<uint8_t, kCredentialIdBytes> credentialId{};
  std::array<uint8_t, kRpIdHashBytes> rpIdHash{};
  uint8_t userHandleLength = 0;
  std::array<uint8_t, kMaxUserHandleBytes> userHandle{};
  P256KeyPair keyPair{};
  uint32_t signCount = 0;
};

class PasskeyCredentialStore {
 public:
  bool begin();
  bool ready() const { return ready_; }

  bool save(const PasskeyCredential& credential);
  bool findByCredentialId(const uint8_t* credentialId, std::size_t credentialIdLength,
                          PasskeyCredential& credential);
  bool findByRpIdHash(const uint8_t rpIdHash[kRpIdHashBytes], PasskeyCredential& credential);
  bool incrementSignCount(const uint8_t* credentialId, std::size_t credentialIdLength, uint32_t& signCount);
  std::size_t credentialCount();

  // Records are authenticated/encrypted with AES-256-GCM, but the current vault
  // root key is still persisted in normal NVS. Hardware-bound HMAC/eFuse key
  // protection is deliberately a separate, explicit provisioning step.
  bool hardwareBacked() const { return false; }

 private:
  bool ready_ = false;
};

PasskeyCredentialStore& credentialStore();

}  // namespace passkey
