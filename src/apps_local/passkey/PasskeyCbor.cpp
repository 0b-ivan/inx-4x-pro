#include "PasskeyCbor.h"

#include <cstring>
#include <limits>

namespace passkey {
namespace {
constexpr unsigned kMaxCborDepth = 8;
constexpr std::size_t kMaxContainerItems = 64;
}

bool CborReader::readHead(uint8_t& major, uint64_t& argument) {
  if (data_ == nullptr || pos_ >= size_) return false;
  const uint8_t initial = data_[pos_++];
  major = static_cast<uint8_t>(initial >> 5U);
  const uint8_t info = static_cast<uint8_t>(initial & 0x1fU);

  if (info < 24U) {
    argument = info;
    return true;
  }
  if (info == 24U) {
    if (pos_ + 1 > size_) return false;
    argument = data_[pos_++];
    return argument >= 24U;  // reject non-canonical small integers/lengths
  }
  if (info == 25U) {
    if (pos_ + 2 > size_) return false;
    argument = (static_cast<uint64_t>(data_[pos_]) << 8U) | data_[pos_ + 1];
    pos_ += 2;
    return argument > 0xffU;
  }
  if (info == 26U) {
    if (pos_ + 4 > size_) return false;
    argument = (static_cast<uint64_t>(data_[pos_]) << 24U) | (static_cast<uint64_t>(data_[pos_ + 1]) << 16U) |
               (static_cast<uint64_t>(data_[pos_ + 2]) << 8U) | data_[pos_ + 3];
    pos_ += 4;
    return argument > 0xffffU;
  }
  // 64-bit lengths/integers and indefinite-length CBOR are unnecessary for a
  // 1200-byte CTAP transport and are rejected to keep parsing bounded.
  return false;
}

bool CborReader::readUnsigned(uint64_t& value) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  if (!readHead(major, value) || major != 0) {
    pos_ = saved;
    return false;
  }
  return true;
}

bool CborReader::readSigned(int64_t& value) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument)) {
    pos_ = saved;
    return false;
  }
  if (major == 0 && argument <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    value = static_cast<int64_t>(argument);
    return true;
  }
  if (major == 1 && argument <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    value = -1 - static_cast<int64_t>(argument);
    return true;
  }
  pos_ = saved;
  return false;
}

bool CborReader::readBytes(const uint8_t*& data, std::size_t& size) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument) || major != 2 || argument > size_ - pos_) {
    pos_ = saved;
    return false;
  }
  data = data_ + pos_;
  size = static_cast<std::size_t>(argument);
  pos_ += size;
  return true;
}

bool CborReader::readText(const char*& data, std::size_t& size) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument) || major != 3 || argument > size_ - pos_) {
    pos_ = saved;
    return false;
  }
  data = reinterpret_cast<const char*>(data_ + pos_);
  size = static_cast<std::size_t>(argument);
  pos_ += size;
  return true;
}

bool CborReader::readArray(std::size_t& size) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument) || major != 4 || argument > kMaxContainerItems) {
    pos_ = saved;
    return false;
  }
  size = static_cast<std::size_t>(argument);
  return true;
}

bool CborReader::readMap(std::size_t& size) {
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument) || major != 5 || argument > kMaxContainerItems) {
    pos_ = saved;
    return false;
  }
  size = static_cast<std::size_t>(argument);
  return true;
}

bool CborReader::readBool(bool& value) {
  if (data_ == nullptr || pos_ >= size_) return false;
  if (data_[pos_] == 0xf4) {
    ++pos_;
    value = false;
    return true;
  }
  if (data_[pos_] == 0xf5) {
    ++pos_;
    value = true;
    return true;
  }
  return false;
}

bool CborReader::skip(const unsigned depth) {
  if (depth > kMaxCborDepth || data_ == nullptr || pos_ >= size_) return false;
  const std::size_t saved = pos_;
  uint8_t major = 0;
  uint64_t argument = 0;
  if (!readHead(major, argument)) {
    pos_ = saved;
    return false;
  }

  if (major <= 1) return true;
  if (major == 2 || major == 3) {
    if (argument > size_ - pos_) {
      pos_ = saved;
      return false;
    }
    pos_ += static_cast<std::size_t>(argument);
    return true;
  }
  if (major == 4) {
    if (argument > kMaxContainerItems) {
      pos_ = saved;
      return false;
    }
    for (uint64_t i = 0; i < argument; ++i) {
      if (!skip(depth + 1)) {
        pos_ = saved;
        return false;
      }
    }
    return true;
  }
  if (major == 5) {
    if (argument > kMaxContainerItems) {
      pos_ = saved;
      return false;
    }
    for (uint64_t i = 0; i < argument * 2U; ++i) {
      if (!skip(depth + 1)) {
        pos_ = saved;
        return false;
      }
    }
    return true;
  }
  if (major == 7 && argument <= 23U) return true;

  pos_ = saved;
  return false;
}

bool CborWriter::putRaw(const uint8_t* data, const std::size_t size) {
  if (data_ == nullptr || (data == nullptr && size != 0) || size > size_ - pos_) return false;
  if (size != 0) std::memcpy(data_ + pos_, data, size);
  pos_ += size;
  return true;
}

bool CborWriter::putHead(const uint8_t major, const uint64_t argument) {
  if (major > 7) return false;
  uint8_t encoded[5]{};
  std::size_t count = 0;
  if (argument < 24U) {
    encoded[0] = static_cast<uint8_t>((major << 5U) | argument);
    count = 1;
  } else if (argument <= 0xffU) {
    encoded[0] = static_cast<uint8_t>((major << 5U) | 24U);
    encoded[1] = static_cast<uint8_t>(argument);
    count = 2;
  } else if (argument <= 0xffffU) {
    encoded[0] = static_cast<uint8_t>((major << 5U) | 25U);
    encoded[1] = static_cast<uint8_t>(argument >> 8U);
    encoded[2] = static_cast<uint8_t>(argument);
    count = 3;
  } else if (argument <= 0xffffffffULL) {
    encoded[0] = static_cast<uint8_t>((major << 5U) | 26U);
    encoded[1] = static_cast<uint8_t>(argument >> 24U);
    encoded[2] = static_cast<uint8_t>(argument >> 16U);
    encoded[3] = static_cast<uint8_t>(argument >> 8U);
    encoded[4] = static_cast<uint8_t>(argument);
    count = 5;
  } else {
    return false;
  }
  return putRaw(encoded, count);
}

bool CborWriter::putUnsigned(const uint64_t value) { return putHead(0, value); }

bool CborWriter::putSigned(const int64_t value) {
  if (value >= 0) return putHead(0, static_cast<uint64_t>(value));
  return putHead(1, static_cast<uint64_t>(-1 - value));
}

bool CborWriter::putBytes(const uint8_t* data, const std::size_t size) {
  return putHead(2, size) && putRaw(data, size);
}

bool CborWriter::putText(const char* data, const std::size_t size) {
  return putHead(3, size) && putRaw(reinterpret_cast<const uint8_t*>(data), size);
}

bool CborWriter::putArray(const std::size_t size) { return putHead(4, size); }
bool CborWriter::putMap(const std::size_t size) { return putHead(5, size); }

bool CborWriter::putBool(const bool value) {
  const uint8_t encoded = value ? 0xf5 : 0xf4;
  return putRaw(&encoded, 1);
}

}  // namespace passkey
