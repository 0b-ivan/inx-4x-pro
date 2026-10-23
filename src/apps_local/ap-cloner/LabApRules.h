#pragma once

#include <cstdint>
#include <string>

// Only intentionally labelled, local test access points may be advertised.
// Passive scans still display other nearby networks for diagnostics.
namespace ap_lab {
inline bool validChannel(uint8_t channel) {
  return channel >= 1 && channel <= 13;
}

inline bool isLabSsid(const std::string& ssid) {
  static const std::string prefix = "X4PRO-LAB-";
  if (ssid.size() <= prefix.size() || ssid.size() > 32 ||
      ssid.compare(0, prefix.size(), prefix) != 0) return false;
  for (size_t i = prefix.size(); i < ssid.size(); ++i) {
    const char c = ssid[i];
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    if (!alpha && !digit && c != '-') return false;
  }
  return true;
}

inline const char* localSsid() { return "X4PRO-LAB-LOCAL"; }
inline uint8_t localChannel() { return 6; }
}  // namespace ap_lab
