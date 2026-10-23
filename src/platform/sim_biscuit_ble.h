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

struct BleGattCharacteristic {
  const char* uuid;
  const char* value;
  bool canRead;
  bool canWrite;
  bool canNotify;
};
struct BleGattService { const char* uuid; };

inline bool bleConnects(const std::string& mac) {
  const char* scenario = std::getenv("CROSSPLAY_SIM_BLE_SCENARIO");
  if (scenario && std::string(scenario) == "disconnect") return false;
  for (const auto& dev : bleAdvertisements()) {
    if (mac == dev.mac) return true;
  }
  return false;
}
inline std::vector<BleGattService> bleGattServices(const std::string& mac) {
  if (!bleConnects(mac)) return {};
  if (mac == "02:00:00:00:00:11") {
    return {{"0000180f-0000-1000-8000-00805f9b34fb"},
            {"0000180a-0000-1000-8000-00805f9b34fb"}};
  }
  if (mac == "02:00:00:00:00:22")
    return {{"00001800-0000-1000-8000-00805f9b34fb"}};
  return {};
}
inline std::vector<BleGattCharacteristic> bleGattCharacteristics(
    const std::string& mac, const std::string& serviceUuid) {
  if (!bleConnects(mac)) return {};
  if (mac == "02:00:00:00:00:11" &&
      serviceUuid == "0000180f-0000-1000-8000-00805f9b34fb")
    return {{"00002a19-0000-1000-8000-00805f9b34fb", "\x55", true, false, true}};
  if (mac == "02:00:00:00:00:11" &&
      serviceUuid == "0000180a-0000-1000-8000-00805f9b34fb")
    return {{"00002a29-0000-1000-8000-00805f9b34fb", "Simulator", true, false, false}};
  if (mac == "02:00:00:00:00:22" &&
      serviceUuid == "00001800-0000-1000-8000-00805f9b34fb")
    return {{"00002a00-0000-1000-8000-00805f9b34fb", "Sim Beacon", true, false, false}};
  return {};
}
}  // namespace biscuit_sim
#endif
