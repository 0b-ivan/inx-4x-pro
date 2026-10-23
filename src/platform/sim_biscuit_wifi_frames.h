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
}  // namespace biscuit_sim
#endif
