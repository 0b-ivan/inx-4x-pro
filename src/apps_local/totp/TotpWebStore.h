#pragma once

#include <cstddef>
#include <cstdint>

namespace totpweb {

constexpr size_t kMaxAccounts = 16;

struct AccountMeta {
  char name[40]{};
  uint8_t digits = 6;
  uint16_t period = 30;
};

enum class Result : uint8_t {
  Ok,
  NoVault,
  BadPin,
  Invalid,
  Full,
  NotFound,
  StorageError,
  Unsupported,
};

const char* resultMessage(Result result);

// All operations decrypt the existing Authenticator vault for one request and
// wipe plaintext/key material before returning. list() intentionally exposes
// metadata only; the Base32 secret is never returned to the web server.
Result list(const char* pin, AccountMeta* output, size_t capacity, size_t& count);
Result add(const char* pin, const char* name, const char* secret, uint8_t digits = 6, uint16_t period = 30);
Result importUri(const char* pin, const char* uri);
Result remove(const char* pin, size_t index);

}  // namespace totpweb
