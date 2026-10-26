#include "RssWifi.h"

#include <WiFi.h>

#include "DevMode.h"
#include "WifiCredentialStore.h"
namespace rsssync {
namespace {
bool connected() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }
}  // namespace
bool WifiSession::connect() {
  if (connected()) return true;
  // An existing AP, join attempt or ESP-NOW session belongs to its caller.
  if (WiFi.getMode() != WIFI_MODE_NULL || devmode::inhibitsSleep()) return false;
  WIFI_STORE.loadFromFile();
  const auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!credential) return false;
  owned = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  const unsigned long start = millis();
  while (!connected() && millis() - start < 20000) delay(50);
  return connected();
}
WifiSession::~WifiSession() {
  if (owned && !devmode::holdsRadio()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}
}  // namespace rsssync
