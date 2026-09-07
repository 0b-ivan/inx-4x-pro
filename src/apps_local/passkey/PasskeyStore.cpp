#include "PasskeyStore.h"

#include <cstring>

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
#include <nvs.h>

namespace passkey {
namespace {

constexpr char kNamespace[] = "x4passkey";
constexpr char kVaultKeyName[] = "vaultkey";
constexpr uint8_t kRecordVersion = 1;
constexpr std::size_t kRecordPlainBytes = 1 + kCredentialIdBytes + kRpIdHashBytes + 1 + kMaxUserHandleBytes +
                                          kP256ScalarBytes + kP256CoordinateBytes + kP256CoordinateBytes + 4;
constexpr std::size_t kEnvelopeBytes = 1 + kGcmNonceBytes + kGcmTagBytes + kRecordPlainBytes;
constexpr std::size_t kNonceOffset = 1;
constexpr std::size_t kTagOffset = kNonceOffset + kGcmNonceBytes;
constexpr std::size_t kCiphertextOffset = kTagOffset + kGcmTagBytes;

nvs_handle_t g_nvs = 0;
bool g_nvsOpen = false;
std::array<uint8_t, kAes256KeyBytes> g_vaultKey{};
std::array<uint8_t, kRecordPlainBytes> g_plain{};
std::array<uint8_t, kEnvelopeBytes> g_envelope{};

enum class SlotRead : uint8_t { Empty, Ok, Corrupt };

void slotKey(const std::size_t slot, char out[4]) {
  out[0] = 'c';
  out[1] = static_cast<char>('0' + ((slot / 10U) % 10U));
  out[2] = static_cast<char>('0' + (slot % 10U));
  out[3] = '\0';
}

void writeBe32(uint8_t* out, const uint32_t value) {
  out[0] = static_cast<uint8_t>(value >> 24U);
  out[1] = static_cast<uint8_t>(value >> 16U);
  out[2] = static_cast<uint8_t>(value >> 8U);
  out[3] = static_cast<uint8_t>(value);
}

uint32_t readBe32(const uint8_t* in) {
  return (static_cast<uint32_t>(in[0]) << 24U) | (static_cast<uint32_t>(in[1]) << 16U) |
         (static_cast<uint32_t>(in[2]) << 8U) | static_cast<uint32_t>(in[3]);
}

bool loadVaultKey() {
  std::size_t length = g_vaultKey.size();
  esp_err_t err = nvs_get_blob(g_nvs, kVaultKeyName, g_vaultKey.data(), &length);
  if (err == ESP_OK) return length == g_vaultKey.size();
  if (err != ESP_ERR_NVS_NOT_FOUND) return false;

  if (!cryptoRandom(g_vaultKey.data(), g_vaultKey.size())) return false;
  if (nvs_set_blob(g_nvs, kVaultKeyName, g_vaultKey.data(), g_vaultKey.size()) != ESP_OK || nvs_commit(g_nvs) != ESP_OK) {
    secureZero(g_vaultKey.data(), g_vaultKey.size());
    return false;
  }
  return true;
}

bool encodeCredential(const PasskeyCredential& credential) {
  if (credential.userHandleLength > kMaxUserHandleBytes) return false;

  g_plain.fill(0);
  std::size_t offset = 0;
  g_plain[offset++] = kRecordVersion;
  std::memcpy(g_plain.data() + offset, credential.credentialId.data(), credential.credentialId.size());
  offset += credential.credentialId.size();
  std::memcpy(g_plain.data() + offset, credential.rpIdHash.data(), credential.rpIdHash.size());
  offset += credential.rpIdHash.size();
  g_plain[offset++] = credential.userHandleLength;
  if (credential.userHandleLength > 0) {
    std::memcpy(g_plain.data() + offset, credential.userHandle.data(), credential.userHandleLength);
  }
  offset += kMaxUserHandleBytes;
  std::memcpy(g_plain.data() + offset, credential.keyPair.privateKey.data(), credential.keyPair.privateKey.size());
  offset += credential.keyPair.privateKey.size();
  std::memcpy(g_plain.data() + offset, credential.keyPair.publicX.data(), credential.keyPair.publicX.size());
  offset += credential.keyPair.publicX.size();
  std::memcpy(g_plain.data() + offset, credential.keyPair.publicY.data(), credential.keyPair.publicY.size());
  offset += credential.keyPair.publicY.size();
  writeBe32(g_plain.data() + offset, credential.signCount);
  offset += 4;
  return offset == g_plain.size();
}

bool decodeCredential(PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  std::size_t offset = 0;
  if (g_plain[offset++] != kRecordVersion) return false;

  std::memcpy(credential.credentialId.data(), g_plain.data() + offset, credential.credentialId.size());
  offset += credential.credentialId.size();
  std::memcpy(credential.rpIdHash.data(), g_plain.data() + offset, credential.rpIdHash.size());
  offset += credential.rpIdHash.size();
  credential.userHandleLength = g_plain[offset++];
  if (credential.userHandleLength > kMaxUserHandleBytes) return false;
  if (credential.userHandleLength > 0) {
    std::memcpy(credential.userHandle.data(), g_plain.data() + offset, credential.userHandleLength);
  }
  offset += kMaxUserHandleBytes;
  std::memcpy(credential.keyPair.privateKey.data(), g_plain.data() + offset, credential.keyPair.privateKey.size());
  offset += credential.keyPair.privateKey.size();
  std::memcpy(credential.keyPair.publicX.data(), g_plain.data() + offset, credential.keyPair.publicX.size());
  offset += credential.keyPair.publicX.size();
  std::memcpy(credential.keyPair.publicY.data(), g_plain.data() + offset, credential.keyPair.publicY.size());
  offset += credential.keyPair.publicY.size();
  credential.signCount = readBe32(g_plain.data() + offset);
  offset += 4;
  return offset == g_plain.size();
}

void makeAad(const std::size_t slot, uint8_t aad[5]) {
  aad[0] = 'X';
  aad[1] = '4';
  aad[2] = 'P';
  aad[3] = 'K';
  aad[4] = static_cast<uint8_t>(slot);
}

bool writeSlot(const std::size_t slot, const PasskeyCredential& credential) {
  if (slot >= kMaxStoredCredentials || !encodeCredential(credential)) return false;

  g_envelope.fill(0);
  g_envelope[0] = kRecordVersion;
  uint8_t* nonce = g_envelope.data() + kNonceOffset;
  uint8_t* tag = g_envelope.data() + kTagOffset;
  uint8_t* ciphertext = g_envelope.data() + kCiphertextOffset;
  uint8_t aad[5];
  makeAad(slot, aad);

  bool ok = cryptoRandom(nonce, kGcmNonceBytes) &&
            aes256GcmEncrypt(g_vaultKey.data(), nonce, g_plain.data(), g_plain.size(), aad, sizeof(aad), ciphertext, tag);
  secureZero(g_plain.data(), g_plain.size());
  if (!ok) {
    secureZero(g_envelope.data(), g_envelope.size());
    return false;
  }

  char key[4];
  slotKey(slot, key);
  ok = nvs_set_blob(g_nvs, key, g_envelope.data(), g_envelope.size()) == ESP_OK && nvs_commit(g_nvs) == ESP_OK;
  secureZero(g_envelope.data(), g_envelope.size());
  return ok;
}

SlotRead readSlot(const std::size_t slot, PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  if (slot >= kMaxStoredCredentials) return SlotRead::Corrupt;

  char key[4];
  slotKey(slot, key);
  std::size_t length = 0;
  esp_err_t err = nvs_get_blob(g_nvs, key, nullptr, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) return SlotRead::Empty;
  if (err != ESP_OK || length != g_envelope.size()) return SlotRead::Corrupt;

  err = nvs_get_blob(g_nvs, key, g_envelope.data(), &length);
  if (err != ESP_OK || length != g_envelope.size() || g_envelope[0] != kRecordVersion) {
    secureZero(g_envelope.data(), g_envelope.size());
    return SlotRead::Corrupt;
  }

  uint8_t aad[5];
  makeAad(slot, aad);
  const bool decrypted = aes256GcmDecrypt(g_vaultKey.data(), g_envelope.data() + kNonceOffset,
                                          g_envelope.data() + kCiphertextOffset, kRecordPlainBytes, aad, sizeof(aad),
                                          g_envelope.data() + kTagOffset, g_plain.data());
  secureZero(g_envelope.data(), g_envelope.size());
  if (!decrypted) {
    secureZero(g_plain.data(), g_plain.size());
    return SlotRead::Corrupt;
  }

  const bool decoded = decodeCredential(credential);
  secureZero(g_plain.data(), g_plain.size());
  if (!decoded) {
    secureZero(&credential, sizeof(credential));
    return SlotRead::Corrupt;
  }
  return SlotRead::Ok;
}

bool credentialIdMatches(const PasskeyCredential& credential, const uint8_t* id, const std::size_t length) {
  return id != nullptr && length == credential.credentialId.size() &&
         std::memcmp(credential.credentialId.data(), id, credential.credentialId.size()) == 0;
}

}  // namespace

bool PasskeyCredentialStore::begin() {
  if (ready_) return true;
  if (!g_nvsOpen) {
    if (nvs_open(kNamespace, NVS_READWRITE, &g_nvs) != ESP_OK) return false;
    g_nvsOpen = true;
  }
  ready_ = loadVaultKey();
  return ready_;
}

bool PasskeyCredentialStore::save(const PasskeyCredential& credential) {
  if (!begin() || credential.userHandleLength > kMaxUserHandleBytes) return false;

  std::size_t freeSlot = kMaxStoredCredentials;
  for (std::size_t slot = 0; slot < kMaxStoredCredentials; ++slot) {
    PasskeyCredential current;
    const SlotRead state = readSlot(slot, current);
    if (state == SlotRead::Empty) {
      if (freeSlot == kMaxStoredCredentials) freeSlot = slot;
      continue;
    }
    if (state == SlotRead::Ok && credentialIdMatches(current, credential.credentialId.data(), credential.credentialId.size())) {
      secureZero(&current, sizeof(current));
      return writeSlot(slot, credential);
    }
    secureZero(&current, sizeof(current));
  }

  return freeSlot < kMaxStoredCredentials && writeSlot(freeSlot, credential);
}

bool PasskeyCredentialStore::findByCredentialId(const uint8_t* credentialId, const std::size_t credentialIdLength,
                                                 PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  if (!begin() || credentialId == nullptr || credentialIdLength != kCredentialIdBytes) return false;

  for (std::size_t slot = 0; slot < kMaxStoredCredentials; ++slot) {
    PasskeyCredential current;
    if (readSlot(slot, current) == SlotRead::Ok && credentialIdMatches(current, credentialId, credentialIdLength)) {
      credential = current;
      secureZero(&current, sizeof(current));
      return true;
    }
    secureZero(&current, sizeof(current));
  }
  return false;
}

bool PasskeyCredentialStore::findByRpIdHash(const uint8_t rpIdHash[kRpIdHashBytes], PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  if (!begin() || rpIdHash == nullptr) return false;

  for (std::size_t slot = 0; slot < kMaxStoredCredentials; ++slot) {
    PasskeyCredential current;
    if (readSlot(slot, current) == SlotRead::Ok &&
        std::memcmp(current.rpIdHash.data(), rpIdHash, current.rpIdHash.size()) == 0) {
      credential = current;
      secureZero(&current, sizeof(current));
      return true;
    }
    secureZero(&current, sizeof(current));
  }
  return false;
}

bool PasskeyCredentialStore::incrementSignCount(const uint8_t* credentialId, const std::size_t credentialIdLength,
                                                 uint32_t& signCount) {
  signCount = 0;
  if (!begin() || credentialId == nullptr || credentialIdLength != kCredentialIdBytes) return false;

  for (std::size_t slot = 0; slot < kMaxStoredCredentials; ++slot) {
    PasskeyCredential current;
    if (readSlot(slot, current) != SlotRead::Ok || !credentialIdMatches(current, credentialId, credentialIdLength)) {
      secureZero(&current, sizeof(current));
      continue;
    }

    if (current.signCount != UINT32_MAX) ++current.signCount;
    signCount = current.signCount;
    const bool saved = writeSlot(slot, current);
    secureZero(&current, sizeof(current));
    return saved;
  }
  return false;
}

std::size_t PasskeyCredentialStore::credentialCount() {
  if (!begin()) return 0;
  std::size_t count = 0;
  for (std::size_t slot = 0; slot < kMaxStoredCredentials; ++slot) {
    PasskeyCredential current;
    if (readSlot(slot, current) == SlotRead::Ok) ++count;
    secureZero(&current, sizeof(current));
  }
  return count;
}

PasskeyCredentialStore& credentialStore() {
  static PasskeyCredentialStore store;
  return store;
}

}  // namespace passkey

#else

namespace passkey {

bool PasskeyCredentialStore::begin() {
  ready_ = false;
  return false;
}
bool PasskeyCredentialStore::save(const PasskeyCredential&) { return false; }
bool PasskeyCredentialStore::findByCredentialId(const uint8_t*, std::size_t, PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  return false;
}
bool PasskeyCredentialStore::findByRpIdHash(const uint8_t[kRpIdHashBytes], PasskeyCredential& credential) {
  credential = PasskeyCredential{};
  return false;
}
bool PasskeyCredentialStore::incrementSignCount(const uint8_t*, std::size_t, uint32_t& signCount) {
  signCount = 0;
  return false;
}
std::size_t PasskeyCredentialStore::credentialCount() { return 0; }
PasskeyCredentialStore& credentialStore() {
  static PasskeyCredentialStore store;
  return store;
}

}  // namespace passkey

#endif
