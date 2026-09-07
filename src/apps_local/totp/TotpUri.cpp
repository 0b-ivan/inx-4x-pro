#include "TotpUri.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "TotpCore.h"

namespace totp {
namespace {

bool hexValue(const char c, uint8_t& value) {
  if (c >= '0' && c <= '9') {
    value = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    value = static_cast<uint8_t>(10 + c - 'a');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    value = static_cast<uint8_t>(10 + c - 'A');
    return true;
  }
  return false;
}

bool percentDecode(const std::string& input, std::string& output) {
  output.clear();
  output.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (c == '+') {
      output.push_back(' ');
      continue;
    }
    if (c != '%') {
      output.push_back(c);
      continue;
    }
    if (i + 2 >= input.size()) return false;
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!hexValue(input[i + 1], hi) || !hexValue(input[i + 2], lo)) return false;
    const char decoded = static_cast<char>((hi << 4U) | lo);
    if (decoded == '\0') return false;
    output.push_back(decoded);
    i += 2;
  }
  return true;
}

bool parseUnsigned(const std::string& value, unsigned long& parsed) {
  if (value.empty()) return false;
  char* end = nullptr;
  parsed = std::strtoul(value.c_str(), &end, 10);
  return end != value.c_str() && *end == '\0';
}

bool equalsIgnoreCase(const std::string& a, const char* b) {
  if (b == nullptr || a.size() != std::strlen(b)) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool parseUri(const char* uri, UriAccount& account) {
  if (uri == nullptr) return false;
  const std::string input(uri);
  constexpr const char* prefix = "otpauth://totp/";
  if (input.rfind(prefix, 0) != 0) return false;

  const size_t queryPos = input.find('?', std::strlen(prefix));
  const std::string encodedLabel = input.substr(std::strlen(prefix), queryPos == std::string::npos
                                                                          ? std::string::npos
                                                                          : queryPos - std::strlen(prefix));
  std::string label;
  if (!percentDecode(encodedLabel, label) || label.empty()) return false;

  std::string secret;
  std::string issuer;
  uint8_t digits = 6;
  uint16_t period = 30;

  if (queryPos != std::string::npos) {
    size_t pos = queryPos + 1;
    while (pos <= input.size()) {
      const size_t amp = input.find('&', pos);
      const std::string pair = input.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      const size_t eq = pair.find('=');
      std::string key;
      std::string value;
      if (eq == std::string::npos) {
        key = pair;
      } else {
        key = pair.substr(0, eq);
        if (!percentDecode(pair.substr(eq + 1), value)) return false;
      }

      if (key == "secret") {
        secret = value;
      } else if (key == "issuer") {
        issuer = value;
      } else if (key == "algorithm" && !value.empty() && !equalsIgnoreCase(value, "SHA1")) {
        return false;
      } else if (key == "digits") {
        unsigned long v = 0;
        if (!parseUnsigned(value, v) || (v != 6 && v != 8)) return false;
        digits = static_cast<uint8_t>(v);
      } else if (key == "period") {
        unsigned long v = 0;
        if (!parseUnsigned(value, v) || v == 0 || v > 300) return false;
        period = static_cast<uint16_t>(v);
      }

      if (amp == std::string::npos) break;
      pos = amp + 1;
    }
  }

  char normalized[kMaxSecretChars + 1]{};
  if (!normalizeSecret(secret.c_str(), normalized, sizeof(normalized))) return false;

  std::string displayName = label;
  if (!issuer.empty() && label.find(':') == std::string::npos) {
    const std::string combined = issuer + ": " + label;
    if (combined.size() <= 39) displayName = combined;
  }
  if (displayName.size() > 39) displayName.resize(39);

  account = UriAccount{};
  account.name = displayName;
  account.secret = normalized;
  account.digits = digits;
  account.period = period;
  return true;
}

}  // namespace totp
