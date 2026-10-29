#pragma once
#include <cstdint>
inline uint32_t esp_random() {
  static uint32_t counter = 100;
  return ++counter;
}
// ESP_RST_POWERON in esp-idf's esp_reset_reason_t.
inline int esp_reset_reason() { return 1; }
