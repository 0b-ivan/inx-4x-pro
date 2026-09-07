#pragma once

#include <cstddef>
#include <cstdint>

namespace passkey {

class CborReader {
 public:
  CborReader(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  bool readUnsigned(uint64_t& value);
  bool readSigned(int64_t& value);
  bool readBytes(const uint8_t*& data, std::size_t& size);
  bool readText(const char*& data, std::size_t& size);
  bool readArray(std::size_t& size);
  bool readMap(std::size_t& size);
  bool readBool(bool& value);
  bool skip(unsigned depth = 0);
  bool finished() const { return pos_ == size_; }
  std::size_t position() const { return pos_; }

 private:
  bool readHead(uint8_t& major, uint64_t& argument);

  const uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 0;
};

class CborWriter {
 public:
  CborWriter(uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  bool putUnsigned(uint64_t value);
  bool putSigned(int64_t value);
  bool putBytes(const uint8_t* data, std::size_t size);
  bool putText(const char* data, std::size_t size);
  bool putArray(std::size_t size);
  bool putMap(std::size_t size);
  bool putBool(bool value);
  std::size_t size() const { return pos_; }

 private:
  bool putHead(uint8_t major, uint64_t argument);
  bool putRaw(const uint8_t* data, std::size_t size);

  uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 0;
};

}  // namespace passkey
