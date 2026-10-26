#pragma once

#include <cstdint>
#include <string>

namespace totp {

struct UriAccount {
  std::string name;
  std::string secret;
  uint8_t digits = 6;
  uint16_t period = 30;
};

// Parse a standard otpauth://totp URI. Only SHA1, 6/8 digits and periods
// between 1 and 300 seconds are accepted because those are the formats the
// on-device authenticator currently persists and renders.
bool parseUri(const char* uri, UriAccount& account);

}  // namespace totp
