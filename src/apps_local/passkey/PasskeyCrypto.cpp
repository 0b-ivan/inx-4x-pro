#include "PasskeyCrypto.h"

#include <cstring>
#include <limits>

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>

namespace passkey {
namespace {

WC_RNG g_rng{};
bool g_rngReady = false;
ecc_key g_eccKey{};
Aes g_aes{};

bool fitsWord32(const std::size_t value) { return value <= std::numeric_limits<word32>::max(); }

bool ensureRng() {
  if (g_rngReady) return true;
  if (wc_InitRng(&g_rng) != 0) return false;
  g_rngReady = true;
  return true;
}

void freeEcc() { wc_ecc_free(&g_eccKey); }

}  // namespace

void secureZero(void* data, const std::size_t length) {
  if (data == nullptr) return;
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  for (std::size_t i = 0; i < length; ++i) p[i] = 0;
}

bool cryptoRandom(uint8_t* out, const std::size_t length) {
  if (out == nullptr || !fitsWord32(length) || !ensureRng()) return false;
  if (length == 0) return true;
  return wc_RNG_GenerateBlock(&g_rng, out, static_cast<word32>(length)) == 0;
}

bool sha256(const uint8_t* data, const std::size_t length, uint8_t out[kSha256Bytes]) {
  if (out == nullptr || (data == nullptr && length != 0) || !fitsWord32(length)) return false;
  static constexpr uint8_t kEmpty = 0;
  const uint8_t* source = data == nullptr ? &kEmpty : data;
  return wc_Sha256Hash(source, static_cast<word32>(length), out) == 0;
}

bool generateP256KeyPair(P256KeyPair& out) {
  out = P256KeyPair{};
  if (!ensureRng()) return false;
  if (wc_ecc_init(&g_eccKey) != 0) return false;

  bool ok = false;
  std::array<uint8_t, kP256ScalarBytes> privateRaw{};
  std::array<uint8_t, 1 + 2 * kP256CoordinateBytes> publicRaw{};
  word32 privateLength = static_cast<word32>(privateRaw.size());
  word32 publicLength = static_cast<word32>(publicRaw.size());

  do {
    if (wc_ecc_make_key_ex(&g_rng, static_cast<int>(kP256ScalarBytes), &g_eccKey, ECC_SECP256R1) != 0) break;
    if (wc_ecc_export_private_only(&g_eccKey, privateRaw.data(), &privateLength) != 0) break;
    if (privateLength == 0 || privateLength > out.privateKey.size()) break;
    if (wc_ecc_export_x963(&g_eccKey, publicRaw.data(), &publicLength) != 0) break;
    if (publicLength != publicRaw.size() || publicRaw[0] != 0x04) break;

    std::memcpy(out.privateKey.data() + (out.privateKey.size() - privateLength), privateRaw.data(), privateLength);
    std::memcpy(out.publicX.data(), publicRaw.data() + 1, out.publicX.size());
    std::memcpy(out.publicY.data(), publicRaw.data() + 1 + out.publicX.size(), out.publicY.size());
    ok = true;
  } while (false);

  secureZero(privateRaw.data(), privateRaw.size());
  secureZero(publicRaw.data(), publicRaw.size());
  freeEcc();
  if (!ok) secureZero(&out, sizeof(out));
  return ok;
}

bool signP256Sha256(const P256KeyPair& keyPair, const uint8_t digest[kSha256Bytes], uint8_t* signature,
                    const std::size_t signatureCapacity, std::size_t& signatureLength) {
  signatureLength = 0;
  if (digest == nullptr || signature == nullptr || signatureCapacity == 0 || !fitsWord32(signatureCapacity) ||
      !ensureRng()) {
    return false;
  }

  std::array<uint8_t, 1 + 2 * kP256CoordinateBytes> publicRaw{};
  publicRaw[0] = 0x04;
  std::memcpy(publicRaw.data() + 1, keyPair.publicX.data(), keyPair.publicX.size());
  std::memcpy(publicRaw.data() + 1 + keyPair.publicX.size(), keyPair.publicY.data(), keyPair.publicY.size());

  if (wc_ecc_init(&g_eccKey) != 0) return false;
  bool ok = false;
  do {
    if (wc_ecc_import_private_key_ex(keyPair.privateKey.data(), static_cast<word32>(keyPair.privateKey.size()),
                                     publicRaw.data(), static_cast<word32>(publicRaw.size()), &g_eccKey,
                                     ECC_SECP256R1) != 0) {
      break;
    }

    word32 outLength = static_cast<word32>(signatureCapacity);
    if (wc_ecc_sign_hash(digest, static_cast<word32>(kSha256Bytes), signature, &outLength, &g_rng, &g_eccKey) != 0) {
      break;
    }
    signatureLength = outLength;
    ok = true;
  } while (false);

  secureZero(publicRaw.data(), publicRaw.size());
  freeEcc();
  if (!ok) secureZero(signature, signatureCapacity);
  return ok;
}

bool aes256GcmEncrypt(const uint8_t key[kAes256KeyBytes], const uint8_t nonce[kGcmNonceBytes],
                      const uint8_t* plaintext, const std::size_t plaintextLength, const uint8_t* aad,
                      const std::size_t aadLength, uint8_t* ciphertext, uint8_t tag[kGcmTagBytes]) {
#if defined(HAVE_AESGCM)
  if (key == nullptr || nonce == nullptr || tag == nullptr || (plaintext == nullptr && plaintextLength != 0) ||
      (ciphertext == nullptr && plaintextLength != 0) || (aad == nullptr && aadLength != 0) ||
      !fitsWord32(plaintextLength) || !fitsWord32(aadLength)) {
    return false;
  }

  if (wc_AesInit(&g_aes, nullptr, INVALID_DEVID) != 0) return false;
  bool ok = false;
  do {
    if (wc_AesGcmSetKey(&g_aes, key, static_cast<word32>(kAes256KeyBytes)) != 0) break;
    if (wc_AesGcmEncrypt(&g_aes, ciphertext, plaintext, static_cast<word32>(plaintextLength), nonce,
                         static_cast<word32>(kGcmNonceBytes), tag, static_cast<word32>(kGcmTagBytes), aad,
                         static_cast<word32>(aadLength)) != 0) {
      break;
    }
    ok = true;
  } while (false);
  wc_AesFree(&g_aes);
  return ok;
#else
  (void)key;
  (void)nonce;
  (void)plaintext;
  (void)plaintextLength;
  (void)aad;
  (void)aadLength;
  (void)ciphertext;
  (void)tag;
  return false;
#endif
}

bool aes256GcmDecrypt(const uint8_t key[kAes256KeyBytes], const uint8_t nonce[kGcmNonceBytes],
                      const uint8_t* ciphertext, const std::size_t ciphertextLength, const uint8_t* aad,
                      const std::size_t aadLength, const uint8_t tag[kGcmTagBytes], uint8_t* plaintext) {
#if defined(HAVE_AESGCM)
  if (key == nullptr || nonce == nullptr || tag == nullptr || (ciphertext == nullptr && ciphertextLength != 0) ||
      (plaintext == nullptr && ciphertextLength != 0) || (aad == nullptr && aadLength != 0) ||
      !fitsWord32(ciphertextLength) || !fitsWord32(aadLength)) {
    return false;
  }

  if (wc_AesInit(&g_aes, nullptr, INVALID_DEVID) != 0) return false;
  bool ok = false;
  do {
    if (wc_AesGcmSetKey(&g_aes, key, static_cast<word32>(kAes256KeyBytes)) != 0) break;
    if (wc_AesGcmDecrypt(&g_aes, plaintext, ciphertext, static_cast<word32>(ciphertextLength), nonce,
                         static_cast<word32>(kGcmNonceBytes), tag, static_cast<word32>(kGcmTagBytes), aad,
                         static_cast<word32>(aadLength)) != 0) {
      break;
    }
    ok = true;
  } while (false);
  wc_AesFree(&g_aes);
  if (!ok && plaintext != nullptr) secureZero(plaintext, ciphertextLength);
  return ok;
#else
  (void)key;
  (void)nonce;
  (void)ciphertext;
  (void)ciphertextLength;
  (void)aad;
  (void)aadLength;
  (void)tag;
  (void)plaintext;
  return false;
#endif
}

}  // namespace passkey

#else

namespace passkey {

void secureZero(void* data, const std::size_t length) {
  if (data == nullptr) return;
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  for (std::size_t i = 0; i < length; ++i) p[i] = 0;
}

bool cryptoRandom(uint8_t*, std::size_t) { return false; }
bool sha256(const uint8_t*, std::size_t, uint8_t[kSha256Bytes]) { return false; }
bool generateP256KeyPair(P256KeyPair& out) {
  secureZero(&out, sizeof(out));
  return false;
}
bool signP256Sha256(const P256KeyPair&, const uint8_t[kSha256Bytes], uint8_t*, std::size_t,
                    std::size_t& signatureLength) {
  signatureLength = 0;
  return false;
}
bool aes256GcmEncrypt(const uint8_t[kAes256KeyBytes], const uint8_t[kGcmNonceBytes], const uint8_t*, std::size_t,
                      const uint8_t*, std::size_t, uint8_t*, uint8_t[kGcmTagBytes]) {
  return false;
}
bool aes256GcmDecrypt(const uint8_t[kAes256KeyBytes], const uint8_t[kGcmNonceBytes], const uint8_t*, std::size_t,
                      const uint8_t*, std::size_t, const uint8_t[kGcmTagBytes], uint8_t*) {
  return false;
}

}  // namespace passkey

#endif
