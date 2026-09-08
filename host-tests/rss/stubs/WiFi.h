#pragma once
#include <string>
constexpr int WL_CONNECTED = 3, WIFI_MODE_NULL = 0, WIFI_STA = 1, WIFI_OFF = 0;
inline unsigned long clockMs = 0;
inline unsigned long millis() { return clockMs; }
inline void delay(unsigned long ms) { clockMs += ms; }
struct IPAddress {
  bool nonzero;
  IPAddress(int a, int b, int c, int d) : nonzero(a || b || c || d) {}
  bool operator!=(const IPAddress& other) const { return nonzero != other.nonzero; }
};
struct FakeWifi {
  int modeValue = WIFI_OFF, statusValue = 0, joins = 0, disconnects = 0;
  bool succeed = true, hasIp = true;
  int status() { return statusValue; }
  int getMode() { return modeValue; }
  IPAddress localIP() { return IPAddress(hasIp && statusValue == WL_CONNECTED, 0, 0, 0); }
  void persistent(bool) {}
  void mode(int m) { modeValue = m; }
  void begin(const char*, const char*) {
    ++joins;
    statusValue = succeed ? WL_CONNECTED : 0;
  }
  void disconnect(bool) {
    ++disconnects;
    statusValue = 0;
  }
};
inline FakeWifi WiFi;
