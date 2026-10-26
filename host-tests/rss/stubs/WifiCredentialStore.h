#pragma once
#include <optional>
#include <string>
struct WifiCredential {
  std::string ssid = "test", password = "test";
};
struct FakeStore {
  bool saved = true;
  void loadFromFile() {}
  std::string getLastConnectedSsid() { return "test"; }
  std::optional<WifiCredential> findCredential(const std::string&) {
    if (saved) return WifiCredential{};
    return std::nullopt;
  }
};
inline FakeStore WIFI_STORE;
