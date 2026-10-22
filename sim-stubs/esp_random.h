#pragma once
#include <stdint.h>
#include <cstddef>
#include <cstring>

#include <random>

// Host stand-in for the ESP32's hardware RNG, used by Developer Mode to mint
// pairing codes and tokens (src/DevMode.cpp).
//
// Unlike the other stubs here this one is REAL rather than a stub that fails:
// nvs_open returning -1 is fine because the simulator has no NVS, but a
// pairing code of 000000 every time would make the simulator quietly wrong
// instead of visibly unsupported, and it is the kind of wrong that reads as
// working. std::random_device is not guaranteed non-deterministic by the
// standard, but on macOS and Linux it is, and nothing on the host side of this
// firmware is a security boundary anyway -- the real device uses the hardware
// RNG.
inline uint32_t esp_random() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
  return dist(gen);
}

// ESP-IDF's bulk RNG API is also used by the SD wipe utility.
// In native simulation, use the host-only RNG above; this is not a
// substitute for the hardware RNG on a production device.
inline void esp_fill_random(void* buffer, size_t length) {
  auto* bytes = static_cast<uint8_t*>(buffer);
  while (length > 0) {
    const uint32_t value = esp_random();
    const size_t count = length < sizeof(value) ? length : sizeof(value);
    std::memcpy(bytes, &value, count);
    bytes += count;
    length -= count;
  }
}
