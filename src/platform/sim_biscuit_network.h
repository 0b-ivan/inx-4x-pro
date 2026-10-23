#pragma once
// Host-only deterministic network fixtures: no external DNS or radios used.
#ifdef SIMULATOR
#include <cstdint>
#include <cstdlib>
#include <string>

namespace biscuit_sim {
inline bool wifiConnected(bool actual) {
  const char* v = std::getenv("CROSSPLAY_SIM_WIFI_CONNECTED");
  if (!v || !*v) return actual;
  if (std::string(v) == "1") return true;
  if (std::string(v) == "0") return false;
  return actual;
}
inline bool resolveHost(const std::string& host, uint8_t (&ip)[4]) {
  if (host == "example.test") {
    ip[0] = 192; ip[1] = 0; ip[2] = 2; ip[3] = 10; return true;
  }
  if (host == "gateway.test") {
    ip[0] = 192; ip[1] = 0; ip[2] = 2; ip[3] = 1; return true;
  }
  if (host == "localhost") {
    ip[0] = 127; ip[1] = 0; ip[2] = 0; ip[3] = 1; return true;
  }
  return false;
}
// This probe never opens a socket or sends radio frames. Non-test
// destinations and non-HTTP ports are deliberately unreachable.
// 192.0.2.0/27 is reserved documentation space. Never probe host sockets.
inline uint32_t hostScanIp(unsigned int lastOctet) {
  return lastOctet >= 1 && lastOctet <= 30
             ? 0x000200C0U | (static_cast<uint32_t>(lastOctet) << 24)
             : 0;
}
inline bool hostScanPortOpen(uint32_t ip, uint16_t port) {
  return port == 80 && (ip == hostScanIp(1) || ip == hostScanIp(20));
}
inline bool tcpProbe(const std::string& host, uint16_t port) {
  return port == 80 && (host == "example.test" || host == "gateway.test" || host == "localhost");
}
}  // namespace biscuit_sim
#endif  // SIMULATOR
