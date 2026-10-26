#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace player {

constexpr size_t kPinSaltSize = 16;
constexpr size_t kPinHashSize = 32;
constexpr size_t kPinCredentialBlobSize = 1 + kPinHashSize;
constexpr int kPinPbkdf2IterationsV1 = 10000;

enum class PinKdfVersion : uint8_t {
  Pbkdf2Sha256V1 = 1,
};

// The DB stores the KDF version beside the derived bytes so later firmware can
// introduce a stronger KDF without invalidating already registered profiles.
struct PinCredential {
  PinKdfVersion version = PinKdfVersion::Pbkdf2Sha256V1;
  std::array<uint8_t, kPinSaltSize> salt{};
  std::array<uint8_t, kPinHashSize> hash{};

  bool supported() const { return version == PinKdfVersion::Pbkdf2Sha256V1; }
};

static_assert(sizeof(PinCredential::salt) == 16, "PIN salt must stay 128-bit");
static_assert(sizeof(PinCredential::hash) == 32, "PIN hash must stay 256-bit");

}  // namespace player
