#include "TotpCore.h"

#include <array>
#include <cctype>
#include <cstring>

namespace totp {
namespace {

constexpr uint32_t rotl(const uint32_t value, const unsigned bits) {
  return (value << bits) | (value >> (32U - bits));
}

class Sha1 {
 public:
  Sha1() = default;

  void update(const uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0) return;
    totalBytes_ += length;
    while (length > 0) {
      const std::size_t room = buffer_.size() - buffered_;
      const std::size_t take = length < room ? length : room;
      std::memcpy(buffer_.data() + buffered_, data, take);
      buffered_ += take;
      data += take;
      length -= take;
      if (buffered_ == buffer_.size()) {
        transform(buffer_.data());
        buffered_ = 0;
      }
    }
  }

  std::array<uint8_t, 20> finish() {
    const uint64_t bitLength = totalBytes_ * 8ULL;
    const uint8_t one = 0x80;
    update(&one, 1);
    const uint8_t zero = 0;
    while (buffered_ != 56) update(&zero, 1);

    uint8_t lengthBytes[8];
    for (int i = 7; i >= 0; --i) {
      lengthBytes[i] = static_cast<uint8_t>(bitLength >> ((7 - i) * 8));
    }
    // The loop above fills backwards from the least significant byte, which is
    // exactly SHA-1's big-endian length field.
    update(lengthBytes, sizeof(lengthBytes));

    std::array<uint8_t, 20> out{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      out[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
      out[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
      out[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
      out[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
    }
    return out;
  }

 private:
  void transform(const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const int o = i * 4;
      w[i] = (static_cast<uint32_t>(block[o]) << 24) | (static_cast<uint32_t>(block[o + 1]) << 16) |
             (static_cast<uint32_t>(block[o + 2]) << 8) | static_cast<uint32_t>(block[o + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];

    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999U;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1U;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCU;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6U;
      }
      const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotl(b, 30);
      b = a;
      a = temp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<uint32_t, 5> state_{0x67452301U, 0xEFCDAB89U, 0x98BADCFEU, 0x10325476U, 0xC3D2E1F0U};
  std::array<uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  uint64_t totalBytes_ = 0;
};

std::array<uint8_t, 20> sha1(const uint8_t* data, const std::size_t length) {
  Sha1 hash;
  hash.update(data, length);
  return hash.finish();
}

std::array<uint8_t, 20> hmacSha1(const uint8_t* key, std::size_t keyLength, const uint8_t* data,
                                 const std::size_t dataLength) {
  std::array<uint8_t, 64> keyBlock{};
  if (keyLength > keyBlock.size()) {
    const auto reduced = sha1(key, keyLength);
    std::memcpy(keyBlock.data(), reduced.data(), reduced.size());
    keyLength = reduced.size();
  } else if (keyLength > 0) {
    std::memcpy(keyBlock.data(), key, keyLength);
  }

  std::array<uint8_t, 64> innerPad{};
  std::array<uint8_t, 64> outerPad{};
  for (std::size_t i = 0; i < keyBlock.size(); ++i) {
    innerPad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x36U);
    outerPad[i] = static_cast<uint8_t>(keyBlock[i] ^ 0x5CU);
  }

  Sha1 inner;
  inner.update(innerPad.data(), innerPad.size());
  inner.update(data, dataLength);
  const auto innerDigest = inner.finish();

  Sha1 outer;
  outer.update(outerPad.data(), outerPad.size());
  outer.update(innerDigest.data(), innerDigest.size());
  return outer.finish();
}

int base32Value(const char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '2' && c <= '7') return 26 + (c - '2');
  return -1;
}

bool decodeBase32(const char* input, uint8_t* output, const std::size_t outputSize, std::size_t& outputLength) {
  outputLength = 0;
  if (input == nullptr || output == nullptr || outputSize == 0) return false;

  uint32_t accumulator = 0;
  unsigned bits = 0;
  for (const char* p = input; *p != '\0'; ++p) {
    const int value = base32Value(*p);
    if (value < 0) return false;
    accumulator = (accumulator << 5) | static_cast<uint32_t>(value);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      if (outputLength >= outputSize) return false;
      output[outputLength++] = static_cast<uint8_t>((accumulator >> bits) & 0xFFU);
    }
  }
  return outputLength > 0;
}

uint32_t decimalModulus(const uint8_t digits) {
  uint32_t value = 1;
  for (uint8_t i = 0; i < digits; ++i) value *= 10U;
  return value;
}

}  // namespace

bool normalizeSecret(const char* input, char* output, const std::size_t outputSize) {
  if (input == nullptr || output == nullptr || outputSize < 2) return false;
  std::size_t used = 0;
  bool paddingStarted = false;

  for (const char* p = input; *p != '\0'; ++p) {
    const unsigned char raw = static_cast<unsigned char>(*p);
    if (raw == ' ' || raw == '\t' || raw == '\r' || raw == '\n' || raw == '-') continue;
    if (raw == '=') {
      paddingStarted = true;
      continue;
    }
    if (paddingStarted) return false;

    char c = static_cast<char>(std::toupper(raw));
    if (base32Value(c) < 0 || used + 1 >= outputSize) return false;
    output[used++] = c;
  }

  if (used == 0) return false;
  output[used] = '\0';

  std::array<uint8_t, 64> decoded{};
  std::size_t decodedLength = 0;
  return decodeBase32(output, decoded.data(), decoded.size(), decodedLength);
}

uint32_t generate(const char* base32Secret, const uint64_t unixSeconds, const uint8_t digits, const uint16_t period,
                  bool* ok) {
  if (ok != nullptr) *ok = false;
  if (base32Secret == nullptr || period == 0 || digits < 6 || digits > 8) return 0;

  std::array<uint8_t, 64> key{};
  std::size_t keyLength = 0;
  if (!decodeBase32(base32Secret, key.data(), key.size(), keyLength)) return 0;

  uint64_t counter = unixSeconds / period;
  uint8_t message[8];
  for (int i = 7; i >= 0; --i) {
    message[i] = static_cast<uint8_t>(counter & 0xFFU);
    counter >>= 8;
  }

  const auto digest = hmacSha1(key.data(), keyLength, message, sizeof(message));
  const uint8_t offset = static_cast<uint8_t>(digest[19] & 0x0FU);
  const uint32_t binary = (static_cast<uint32_t>(digest[offset] & 0x7FU) << 24) |
                          (static_cast<uint32_t>(digest[offset + 1]) << 16) |
                          (static_cast<uint32_t>(digest[offset + 2]) << 8) |
                          static_cast<uint32_t>(digest[offset + 3]);

  volatile uint8_t* wipe = key.data();
  for (std::size_t i = 0; i < key.size(); ++i) wipe[i] = 0;

  if (ok != nullptr) *ok = true;
  return binary % decimalModulus(digits);
}

uint16_t secondsRemaining(const uint64_t unixSeconds, const uint16_t period) {
  if (period == 0) return 0;
  return static_cast<uint16_t>(period - (unixSeconds % period));
}

}  // namespace totp
