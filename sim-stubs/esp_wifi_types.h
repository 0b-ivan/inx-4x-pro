#pragma once
#include <cstdint>
// Minimal esp_wifi_types stubs. Only the struct declaration is needed;
// the .cpp files that access the fields are excluded via build_src_filter.
struct wifi_pkt_rx_ctrl_t {
  int8_t rssi;
  uint8_t channel;
  uint16_t sig_len;
};
struct wifi_promiscuous_pkt_t {
  wifi_pkt_rx_ctrl_t rx_ctrl;
  uint8_t payload[0];
};
