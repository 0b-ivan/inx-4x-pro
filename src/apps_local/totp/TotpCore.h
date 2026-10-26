#pragma once

#include <cstddef>
#include <cstdint>

namespace totp {

constexpr std::size_t kMaxSecretChars = 96;

// Normalises a Base32 TOTP secret to upper-case without spaces, '-' or '='
// padding. Returns false for an empty, invalid or oversized secret.
bool normalizeSecret(const char* input, char* output, std::size_t outputSize);

// RFC 6238 / HMAC-SHA1. `digits` is normally 6 or 8 and `period` normally 30.
// `ok` is set false when the secret or parameters are invalid.
uint32_t generate(const char* base32Secret, uint64_t unixSeconds, uint8_t digits = 6, uint16_t period = 30,
                  bool* ok = nullptr);

// Seconds left in the current TOTP window, in the range 1..period.
uint16_t secondsRemaining(uint64_t unixSeconds, uint16_t period = 30);

}  // namespace totp
