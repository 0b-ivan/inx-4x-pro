#include <cstdint>
#include <cstdio>
#include <cstring>

#include "totp/TotpCore.h"

namespace {
int checks = 0;
int failed = 0;

void expect(const bool condition, const char* name) {
  ++checks;
  if (!condition) {
    ++failed;
    std::printf("FAIL totp  %s\n", name);
  }
}
}  // namespace

int main() {
  // RFC 6238 Appendix B, SHA-1 secret "12345678901234567890" in Base32.
  constexpr char secret[] = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
  struct Vector {
    uint64_t at;
    uint32_t code;
  };
  constexpr Vector vectors[] = {
      {59ULL, 94287082U},
      {1111111109ULL, 7081804U},
      {1111111111ULL, 14050471U},
      {1234567890ULL, 89005924U},
      {2000000000ULL, 69279037U},
      {20000000000ULL, 65353130U},
  };

  for (const auto& vector : vectors) {
    bool ok = false;
    const uint32_t actual = totp::generate(secret, vector.at, 8, 30, &ok);
    expect(ok && actual == vector.code, "RFC 6238 SHA1 vector");
  }

  bool ok = false;
  expect(totp::generate(secret, 59ULL, 6, 30, &ok) == 287082U && ok, "six digit truncation");

  char normalized[totp::kMaxSecretChars + 1]{};
  expect(totp::normalizeSecret("gezd gnbv-gy3tqojq====", normalized, sizeof(normalized)), "normalise common formatting");
  expect(std::strcmp(normalized, "GEZDGNBVGY3TQOJQ") == 0, "normalised value");
  expect(!totp::normalizeSecret("NOT*BASE32", normalized, sizeof(normalized)), "reject non-base32 character");
  expect(!totp::normalizeSecret("", normalized, sizeof(normalized)), "reject empty secret");
  expect(totp::secondsRemaining(59ULL, 30) == 1, "one second before boundary");
  expect(totp::secondsRemaining(60ULL, 30) == 30, "new window starts full");

  std::printf("totp: %d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
