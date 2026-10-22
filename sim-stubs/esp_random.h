#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
// esp_random.h stub for the simulator: redirect to host PRNG.
// Cryptographic quality is not required in the simulator.
inline uint32_t esp_random() { return static_cast<uint32_t>(rand()); }
inline void esp_fill_random(void* buf, size_t len) {
  auto* p = static_cast<unsigned char*>(buf);
  for (size_t i = 0; i < len; ++i) p[i] = static_cast<unsigned char>(rand());
}
