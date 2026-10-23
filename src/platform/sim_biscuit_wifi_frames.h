#pragma once
// Deterministic, passive 802.11 probe-request fixtures. Native simulator only.
// This code neither opens a socket nor interfaces with any wireless controller.
#ifdef SIMULATOR
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace biscuit_sim {
struct ProbeFrame {
  std::vector<uint8_t> bytes;
  int rssi;
};
struct ParsedProbe {
  uint8_t mac[6]{};
  std::string ssid;
};
inline ProbeFrame makeProbe(const uint8_t mac[6], const std::string& ssid, int rssi) {
  const size_t len = ssid.size() > 32 ? 32 : ssid.size();
  ProbeFrame frame{std::vector<uint8_t>(26 + len, 0), rssi};
  frame.bytes[0] = 0x40; // management frame, subtype 4 (probe request)
  std::memcpy(frame.bytes.data() + 10, mac, 6);
  frame.bytes[24] = 0; // SSID tagged parameter
  frame.bytes[25] = static_cast<uint8_t>(len);
  std::memcpy(frame.bytes.data() + 26, ssid.data(), len);
  return frame;
}
inline bool parseProbe(const uint8_t* bytes, size_t len, ParsedProbe& out) {
  if (!bytes || len < 26 || (bytes[0] & 0xFC) != 0x40) return false;
  if (bytes[24] != 0 || bytes[25] > 32 || len < 26u + bytes[25])
    return false;
  std::memcpy(out.mac, bytes + 10, 6);
  out.ssid.assign(reinterpret_cast<const char*>(bytes + 26), bytes[25]);
  return true;
}
inline std::vector<ProbeFrame> probeFrames() {
  const char* env = std::getenv("CROSSPLAY_SIM_PROBE_SCENARIO");
  const std::string scenario = env ? env : "normal";
  if (scenario == "empty") return {};
  const uint8_t macA[6] = {0x02,0,0,0,0,0x31};
  const uint8_t macB[6] = {0x02,0,0,0,0,0x32};
  const uint8_t macC[6] = {0x02,0,0,0,0,0x33};
  auto wrongType = makeProbe(macA, "Fake", -80);
  wrongType.bytes[0] = 0x80; // beacon, not a probe request
  auto truncated = makeProbe(macB, "Bad", -85);
  truncated.bytes[25] = 20; // invalid claimed SSID size
  if (scenario == "malformed") return {wrongType, truncated};
  auto first = makeProbe(macA, "LabNetwork", -45);
  return {first, makeProbe(macB, "", -73), makeProbe(macC, "GuestLab", -62),
          first, wrongType, truncated};
}

// Independent OS-heuristic fixtures: never change the Probe Sniffer scenarios.
inline std::vector<ProbeFrame> fingerprintFrames() {
  const char* raw = std::getenv("CROSSPLAY_SIM_FINGERPRINT_SCENARIO");
  const std::string scenario = raw ? raw : "normal";
  if (scenario == "empty") return {};
  const uint8_t ios[6] = {0x02,0,0,0,0,0x41};
  const uint8_t android[6] = {0x06,0,0,0,0,0x42};
  const uint8_t windows[6] = {0x00,0x1b,0x21,0,0,0x43};
  const uint8_t linuxDevice[6] = {0xb8,0x27,0xeb,0,0,0x44};
  const uint8_t unknown[6] = {0x10,0x22,0x33,0,0,0x45};
  auto beacon = makeProbe(ios, "IgnoreBeacon", -80);
  beacon.bytes[0] = 0x80; // not a probe request
  auto truncated = makeProbe(android, "BadTag", -85);
  truncated.bytes[25] = 32; // 802.11 tag claims missing data
  if (scenario == "malformed") return {beacon, truncated};
  const auto localIOS = makeProbe(ios, "Lab-iOS", -50);
  const auto localAndroid = makeProbe(android, "Lab-Android", -62);
  return {
    localIOS, localIOS,
    localAndroid, localAndroid, localAndroid, localAndroid,
    makeProbe(windows, "Lab-Windows", -48),
    makeProbe(linuxDevice, "Lab-Linux", -71),
    makeProbe(unknown, "Lab-Unknown", -82),
    beacon, truncated
  };
}

// Passive deauthentication/disassociation monitor fixtures.
// 802.11 management headers are 24 bytes; no frame is ever transmitted.
inline ProbeFrame managementFrame(uint8_t frameControl, uint8_t id, int rssi) {
  ProbeFrame frame{std::vector<uint8_t>(26, 0), rssi};
  frame.bytes[0] = frameControl;
  frame.bytes[4] = 0x02; // synthetic receiver
  frame.bytes[9] = 0x11;
  frame.bytes[10] = 0x02; // synthetic transmitter
  frame.bytes[15] = id;
  frame.bytes[16] = 0x02; // synthetic BSSID
  frame.bytes[21] = 0x22;
  frame.bytes[24] = 0x07; // reason code (simulator only)
  return frame;
}
inline std::vector<ProbeFrame> deauthMonitorFrames() {
  const char* env = std::getenv("CROSSPLAY_SIM_DEAUTH_SCENARIO");
  const std::string scenario = env ? env : "normal";
  if (scenario == "empty") return {};
  auto truncated = managementFrame(0xc0, 1, -80);
  truncated.bytes.resize(12);
  if (scenario == "malformed")
    return {truncated, managementFrame(0x08, 2, -68)}; // data frame ignored
  if (scenario == "spike") {
    std::vector<ProbeFrame> frames;
    for (uint8_t i = 0; i < 6; ++i)
      frames.push_back(managementFrame(0xc0, static_cast<uint8_t>(i + 1), -50 - i));
    frames.push_back(managementFrame(0xa0, 8, -65));
    return frames;
  }
  return {managementFrame(0xc0, 1, -48),
          managementFrame(0xa0, 2, -58),
          managementFrame(0xc0, 3, -62),
          managementFrame(0x80, 4, -75), // beacon: counted as management, not deauth
          truncated, managementFrame(0x08, 5, -80)};
}

// Passive Packet Monitor input (never read from, or transmitted by, host radio).
// Includes a repeated source MAC and a deliberately truncated frame.
inline std::vector<ProbeFrame> packetMonitorFrames() {
  const char* raw = std::getenv("CROSSPLAY_SIM_PACKET_SCENARIO");
  const std::string scenario = raw ? raw : "normal";
  if (scenario == "empty") return {};
  ProbeFrame malformed{std::vector<uint8_t>(8, 0), -90};
  if (scenario == "malformed") return {malformed};
  const uint8_t a[6] = {0x02,0,0,0,0,0x61};
  const uint8_t b[6] = {0x02,0,0,0,0,0x62};
  const uint8_t c[6] = {0x02,0,0,0,0,0x63};
  return {makeProbe(a, "Lab-A", -45), makeProbe(b, "Lab-B", -65),
          makeProbe(a, "Lab-A", -43), makeProbe(c, "", -80),
          malformed};
}

// Offline-only synthetic APs used for WiFi Deauth UI preview. Never TX.
struct SimLabAp {
  const char* ssid;
  uint8_t bssid[6];
  int rssi;
  uint8_t channel;
};
inline std::vector<SimLabAp> deauthPreviewAps() {
  const char* scenario = std::getenv("CROSSPLAY_SIM_WIFI_DEAUTH_SCENARIO");
  if (scenario && std::string(scenario) == "empty") return {};
  return {
    {"LabAP", {0x02,0,0,0,0,0x71}, -48, 1},
    {"GuestLab", {0x02,0,0,0,0,0x72}, -72, 6}
  };
}
}  // namespace biscuit_sim
#endif
