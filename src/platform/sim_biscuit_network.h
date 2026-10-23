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
}  // namespace biscuit_sim
#endif  // SIMULATOR
