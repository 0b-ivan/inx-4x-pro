#pragma once
#include <cstdint>
// Minimal esp_now stubs for hardware-only activity headers.
// The .cpp files that call these are excluded via build_src_filter.
struct esp_now_recv_info_t {
  uint8_t src_addr[6];
  uint8_t des_addr[6];
};
