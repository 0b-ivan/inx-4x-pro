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

inline std::string makeBleContactPacket(unsigned company, const std::string& name,
                                         const std::string& phone) {
  std::string packet(29, '\0');  // 2 byte ID, 14 byte name, 13 byte phone.
  packet[0] = static_cast<char>(company & 255);
  packet[1] = static_cast<char>((company >> 8) & 255);
  for (size_t i = 0; i < name.size() && i < 14; ++i) packet[2+i] = name[i];
  for (size_t i = 0; i < phone.size() && i < 13; ++i) packet[16+i] = phone[i];
  return packet;
}
inline std::vector<std::string> bleContactPackets() {
  const char* v = std::getenv("CROSSPLAY_SIM_BLE_SCENARIO");
  const std::string scenario = v ? v : "normal";
  if (scenario == "empty" || scenario == "timeout" || scenario == "disconnect") return {};
  return {
    makeBleContactPacket(0xB1CC, "Sim Alice", "TEST-0001"),
    makeBleContactPacket(0xDEAD, "Wrong ID", "TEST-9999"),
    makeBleContactPacket(0xB1CC, "Sim Bob", "TEST-0002"),
    makeBleContactPacket(0xB1CC, "Sim Alice", "TEST-0001"),
    makeBleContactPacket(0xB1CC, "Sim Operator", "TEST-SELF"),
    std::string("short")
  };
}

struct BleTrackerFrame {
  const char* mac;
  int rssi;
  std::string manufacturer;
  const char* serviceUuid;
};
inline bool bleTrackerFollowerScenario() {
  const char* value = std::getenv("CROSSPLAY_SIM_TRACKER_SCENARIO");
  return value && std::string(value) == "follower";
}
inline std::vector<BleTrackerFrame> bleTrackerFrames() {
  const char* value = std::getenv("CROSSPLAY_SIM_TRACKER_SCENARIO");
  const std::string scenario = value ? value : "normal";
  if (scenario == "empty" || scenario == "timeout") return {};
  const BleTrackerFrame simulatedTag{
      "02:00:00:00:00:51", -51,
      std::string("\x4c\x00\x12\x19\x01", 5), ""};
  if (scenario == "follower") return {simulatedTag};
  return {
    simulatedTag,
    {"02:00:00:00:00:52", -64, std::string("\x75\x00", 2), ""},
    {"02:00:00:00:00:53", -73, "", "0000feed-0000-1000-8000-00805f9b34fb"},
    {"02:00:00:00:00:54", -90, std::string("\x01\x00", 2), ""} // ignored
  };
}

// Deliberately non-wire-compatible demo payload. Never emit as BLE adverts.
// Mode transitions can be tested without impersonating a real device.
struct SimBlePreview {
  int family;
  uint32_t sequence;
  std::string bytes;
};
inline SimBlePreview blePreview(int family, uint32_t sequence) {
  const auto label = std::string("OFFLINE-SIM:") + std::to_string(family) +
                     ":" + std::to_string(sequence);
  return {family, sequence, label};
}
}  // namespace biscuit_sim
#endif
