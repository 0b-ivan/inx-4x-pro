#pragma once
// Native SDL-only BLE fixtures. No host BLE controller, sockets or peripherals.
#ifdef SIMULATOR
#include <cstdlib>
#include <string>
#include <vector>

namespace biscuit_sim {
struct BleAdvertisement {
  const char* name;
  const char* mac;
  int rssi;
};

inline std::vector<BleAdvertisement> bleAdvertisements() {
  const char* raw = std::getenv("CROSSPLAY_SIM_BLE_SCENARIO");
  const std::string scenario = raw ? raw : "normal";
  if (scenario == "empty" || scenario == "timeout") return {};
  if (scenario == "weak") return {
      {"Sim Sensor", "02:00:00:00:00:11", -93},
      {"Sim Beacon", "02:00:00:00:00:22", -87},
  };
  return {
      {"Sim Sensor", "02:00:00:00:00:11", -48},
      {"Sim Beacon", "02:00:00:00:00:22", -75},
  };
}
}  // namespace biscuit_sim
#endif
