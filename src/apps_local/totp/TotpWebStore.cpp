#include "TotpWebStore.h"

#include <Memory.h>

#include <array>
#include <cstdio>
#include <cstring>

#include "TotpCore.h"
#include "TotpUri.h"

#if !defined(SIMULATOR)
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#endif

namespace totpweb {
namespace {

struct Account {
  char name[40]{};
  char secret[totp::kMaxSecretChars + 1]{};
  uint8_t digits = 6;
  uint8_t reserved = 0;
  uint16_t period = 30;
};

constexpr uint32_t kStoreMagic = 0x54505431U;
constexpr uint16_t kStoreVersion = 1;
struct StoreBlob {
  uint32_t magic = kStoreMagic;
  uint16_t version = kStoreVersion;
  uint16_t count = 0;
  std::array<Account, kMaxAccounts> accounts{};
};

#if !defined(SIMULATOR)
constexpr uint32_t kVaultMagic = 0x32565054U;
constexpr uint16_t kVaultVersion = 2;
constexpr size_t kSaltBytes = 16;
constexpr size_t kIvBytes = 12;
constexpr size_t kTagBytes = 16;
constexpr size_t kKeyBytes = 32;
constexpr unsigned int kPbkdf2Iterations = 120000;

struct VaultBlob {
  uint32_t magic = kVaultMagic;
  uint16_t version = kVaultVersion;
  uint16_t reserved = 0;
  std::array<uint8_t, kSaltBytes> salt{};
  std::array<uint8_t, kIvBytes> iv{};
  std::array<uint8_t, kTagBytes> tag{};
  std::array<uint8_t, sizeof(StoreBlob)> ciphertext{};
};
#endif

void wipe(void* data, size_t len) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  while (len-- > 0) *p++ = 0;
}

bool validPin(const char* pin) {
  if (pin == nullptr) return false;
  const size_t len = std::strlen(pin);
  if (len < 6 || len > 12) return false;
  for (size_t i = 0; i < len; ++i) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }
  return true;
}

bool validate(StoreBlob& store) {
  if (store.magic != kStoreMagic || store.version != kStoreVersion || store.count > kMaxAccounts) return false;
  for (uint16_t i = 0; i < store.count; ++i) {
    Account& account = store.accounts[i];
    account.name[sizeof(account.name) - 1] = '\0';
    account.secret[sizeof(account.secret) - 1] = '\0';
    if (account.name[0] == '\0' || account.period == 0 || (account.digits != 6 && account.digits != 8)) return false;
    char normalized[totp::kMaxSecretChars + 1]{};
    if (!totp::normalizeSecret(account.secret, normalized, sizeof(normalized))) return false;
    std::snprintf(account.secret, sizeof(account.secret), "%s", normalized);
  }
  return true;
}

#if !defined(SIMULATOR)
Result load(const char* pin, StoreBlob& store, VaultBlob& vault, std::array<uint8_t, kKeyBytes>& key) {
  if (!validPin(pin)) return Result::BadPin;
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", true)) return Result::StorageError;
  const size_t bytes = prefs.getBytesLength("vault");
  if (bytes == 0) {
    prefs.end();
    return Result::NoVault;
  }
  const bool readOk = bytes == sizeof(VaultBlob) && prefs.getBytes("vault", &vault, sizeof(vault)) == sizeof(vault);
  prefs.end();
  if (!readOk || vault.magic != kVaultMagic || vault.version != kVaultVersion) return Result::StorageError;

  if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(pin), std::strlen(pin),
                                     vault.salt.data(), vault.salt.size(), kPbkdf2Iterations,
                                     static_cast<uint32_t>(key.size()), key.data()) != 0) {
    return Result::StorageError;
  }

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, sizeof(StoreBlob), vault.iv.data(), vault.iv.size(), nullptr, 0,
                                  vault.tag.data(), vault.tag.size(), vault.ciphertext.data(),
                                  reinterpret_cast<unsigned char*>(&store));
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return Result::BadPin;
  return validate(store) ? Result::Ok : Result::StorageError;
}

Result save(StoreBlob& store, VaultBlob& vault, const std::array<uint8_t, kKeyBytes>& key) {
  esp_fill_random(vault.iv.data(), vault.iv.size());
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(StoreBlob), vault.iv.data(), vault.iv.size(),
                                   nullptr, 0, reinterpret_cast<const unsigned char*>(&store), vault.ciphertext.data(),
                                   vault.tag.size(), vault.tag.data());
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return Result::StorageError;

  Preferences prefs;
  if (!prefs.begin("crossplay-totp", false)) return Result::StorageError;
  const size_t written = prefs.putBytes("vault", &vault, sizeof(vault));
  prefs.end();
  return written == sizeof(vault) ? Result::Ok : Result::StorageError;
}
#endif

Result mutateAdd(const char* pin, const char* name, const char* secret, uint8_t digits, uint16_t period) {
#if defined(SIMULATOR)
  (void)pin; (void)name; (void)secret; (void)digits; (void)period;
  return Result::Unsupported;
#else
  if (name == nullptr || name[0] == '\0' || std::strlen(name) > 39 || period == 0 || period > 300 ||
      (digits != 6 && digits != 8)) return Result::Invalid;
  char normalized[totp::kMaxSecretChars + 1]{};
  if (!totp::normalizeSecret(secret, normalized, sizeof(normalized))) return Result::Invalid;

  auto store = makeUniqueNoThrow<StoreBlob>();
  auto vault = makeUniqueNoThrow<VaultBlob>();
  if (!store || !vault) return Result::StorageError;
  std::array<uint8_t, kKeyBytes> key{};
  Result result = load(pin, *store, *vault, key);
  if (result == Result::Ok) {
    if (store->count >= kMaxAccounts) {
      result = Result::Full;
    } else {
      Account& account = store->accounts[store->count];
      account = Account{};
      std::snprintf(account.name, sizeof(account.name), "%s", name);
      std::snprintf(account.secret, sizeof(account.secret), "%s", normalized);
      account.digits = digits;
      account.period = period;
      ++store->count;
      result = save(*store, *vault, key);
    }
  }
  wipe(normalized, sizeof(normalized));
  wipe(key.data(), key.size());
  wipe(store.get(), sizeof(StoreBlob));
  return result;
#endif
}

}  // namespace

const char* resultMessage(Result result) {
  switch (result) {
    case Result::Ok: return "ok";
    case Result::NoVault: return "No encrypted Authenticator vault exists yet";
    case Result::BadPin: return "Invalid vault PIN";
    case Result::Invalid: return "Invalid TOTP account data";
    case Result::Full: return "Authenticator vault is full";
    case Result::NotFound: return "TOTP account not found";
    case Result::StorageError: return "Authenticator vault could not be read or written";
    case Result::Unsupported: return "TOTP web management is unavailable in this build";
  }
  return "Unknown error";
}

Result list(const char* pin, AccountMeta* output, size_t capacity, size_t& count) {
  count = 0;
#if defined(SIMULATOR)
  (void)pin; (void)output; (void)capacity;
  return Result::Unsupported;
#else
  auto store = makeUniqueNoThrow<StoreBlob>();
  auto vault = makeUniqueNoThrow<VaultBlob>();
  if (!store || !vault) return Result::StorageError;
  std::array<uint8_t, kKeyBytes> key{};
  Result result = load(pin, *store, *vault, key);
  if (result == Result::Ok) {
    if (output == nullptr || capacity < store->count) {
      result = Result::StorageError;
    } else {
      count = store->count;
      for (size_t i = 0; i < count; ++i) {
        std::snprintf(output[i].name, sizeof(output[i].name), "%s", store->accounts[i].name);
        output[i].digits = store->accounts[i].digits;
        output[i].period = store->accounts[i].period;
      }
    }
  }
  wipe(key.data(), key.size());
  wipe(store.get(), sizeof(StoreBlob));
  return result;
#endif
}

Result add(const char* pin, const char* name, const char* secret, uint8_t digits, uint16_t period) {
  return mutateAdd(pin, name, secret, digits, period);
}

Result importUri(const char* pin, const char* uri) {
  totp::UriAccount account;
  if (!totp::parseUri(uri, account)) return Result::Invalid;
  const Result result = mutateAdd(pin, account.name.c_str(), account.secret.c_str(), account.digits, account.period);
  if (!account.secret.empty()) wipe(account.secret.data(), account.secret.size());
  return result;
}

Result remove(const char* pin, size_t index) {
#if defined(SIMULATOR)
  (void)pin; (void)index;
  return Result::Unsupported;
#else
  auto store = makeUniqueNoThrow<StoreBlob>();
  auto vault = makeUniqueNoThrow<VaultBlob>();
  if (!store || !vault) return Result::StorageError;
  std::array<uint8_t, kKeyBytes> key{};
  Result result = load(pin, *store, *vault, key);
  if (result == Result::Ok) {
    if (index >= store->count) {
      result = Result::NotFound;
    } else {
      for (size_t i = index; i + 1 < store->count; ++i) store->accounts[i] = store->accounts[i + 1];
      --store->count;
      store->accounts[store->count] = Account{};
      result = save(*store, *vault, key);
    }
  }
  wipe(key.data(), key.size());
  wipe(store.get(), sizeof(StoreBlob));
  return result;
#endif
}

}  // namespace totpweb
