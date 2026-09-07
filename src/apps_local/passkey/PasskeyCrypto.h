#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace passkey {

constexpr std::size_t kSha256Bytes = 32;
constexpr std::size_t kP256ScalarBytes = 32;
constexpr std::size_t kP256CoordinateBytes = 32;
constexpr std::size_t kAes256KeyBytes = 32;
constexpr std::size_t kGcmNonceBytes = 12;
constexpr std::size_t kGcmTagBytes = 16;
constexpr std::size_t kMaxEs256DerSignatureBytes = 80;

struct P256KeyPair {
  std::array<uint8_t, kP256ScalarBytes> privateKey{};
  std::array<uint8_t, kP256CoordinateBytes> publicX{};
  std::array<uint8_t, kP256CoordinateBytes> publicY{};
};

// The passkey transport serializes crypto operations on its worker task. These
// helpers intentionally keep their wolfCrypt contexts in static storage rather
// than putting the relatively large contexts on the FreeRTOS task stack.
bool cryptoRandom(uint8_t* out, std::size_t length);
bool sha256(const uint8_t* data, std::size_t length, uint8_t out[kSha256Bytes]);
bool generateP256KeyPair(P256KeyPair& out);
bool signP256Sha256(const P256KeyPair& keyPair, const uint8_t digest[kSha256Bytes], uint8_t* signature,
                    std::size_t signatureCapacity, std::size_t& signatureLength);

bool aes256GcmEncrypt(const uint8_t key[kAes256KeyBytes], const uint8_t nonce[kGcmNonceBytes],
                      const uint8_t* plaintext, std::size_t plaintextLength, const uint8_t* aad,
                      std::size_t aadLength, uint8_t* ciphertext, uint8_t tag[kGcmTagBytes]);
bool aes256GcmDecrypt(const uint8_t key[kAes256KeyBytes], const uint8_t nonce[kGcmNonceBytes],
                      const uint8_t* ciphertext, std::size_t ciphertextLength, const uint8_t* aad,
                      std::size_t aadLength, const uint8_t tag[kGcmTagBytes], uint8_t* plaintext);

void secureZero(void* data, std::size_t length);

}  // namespace passkey
