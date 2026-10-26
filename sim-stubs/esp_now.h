#pragma once
// Native X4 Pro simulator ONLY: self-contained ESP-NOW transport in memory.
// This header shadows the hardware ESP-NOW API under platformio.sim.ini.
// No sockets, WiFi interfaces, radio controllers or outbound packets.
#include <cstdint>
#include <cstring>
#ifndef ESP_OK
#define ESP_OK 0
#endif

struct esp_now_recv_info_t {
  uint8_t src_addr[6]{};
  uint8_t des_addr[6]{};
};
struct esp_now_peer_info_t {
  uint8_t peer_addr[6]{};
  uint8_t channel = 0;
  bool encrypt = false;
};
using esp_now_recv_cb_t =
    void (*)(const esp_now_recv_info_t*, const uint8_t*, int);

namespace biscuit_sim {
inline esp_now_recv_cb_t esp_now_callback = nullptr;
inline bool esp_now_active = false;
inline unsigned esp_now_sent = 0;
inline unsigned esp_now_delivered = 0;
}
inline int esp_now_init() {
  biscuit_sim::esp_now_active = true;
  biscuit_sim::esp_now_sent = 0;
  biscuit_sim::esp_now_delivered = 0;
  return ESP_OK;
}
inline void esp_now_register_recv_cb(esp_now_recv_cb_t callback) {
  biscuit_sim::esp_now_callback = callback;
}
inline void esp_now_unregister_recv_cb() {
  biscuit_sim::esp_now_callback = nullptr;
}
inline int esp_now_add_peer(const esp_now_peer_info_t*) {
  return biscuit_sim::esp_now_active ? ESP_OK : -1;
}
inline int esp_now_send(const uint8_t*, const uint8_t*, unsigned) {
  if (!biscuit_sim::esp_now_active) return -1;
  ++biscuit_sim::esp_now_sent;
  return ESP_OK;  // virtual TX counter only, NEVER send
}
inline int esp_now_deinit() {
  biscuit_sim::esp_now_active = false;
  biscuit_sim::esp_now_callback = nullptr;
  return ESP_OK;
}
inline bool esp_now_sim_deliver(const uint8_t mac[6], const uint8_t* data, int len) {
  if (!biscuit_sim::esp_now_active || !biscuit_sim::esp_now_callback ||
      !mac || !data || len < 0) return false;
  esp_now_recv_info_t info{};
  std::memcpy(info.src_addr, mac, 6);
  std::memset(info.des_addr, 0xff, 6);
  biscuit_sim::esp_now_callback(&info, data, len);
  ++biscuit_sim::esp_now_delivered;
  return true;
}
