#include <WiFi.h>

#include <cassert>
#include <cstdio>

#include "DevMode.h"
#include "RssWifi.h"
#include "WifiCredentialStore.h"
void reset() {
  WiFi = {};
  WIFI_STORE = {};
  devmode::enabled = devmode::held = false;
  clockMs = 0;
}
int main() {
  reset();
  {
    rsssync::WifiSession wifi;
    assert(wifi.connect());
    assert(WiFi.joins == 1);
  }
  assert(WiFi.getMode() == WIFI_OFF && WiFi.disconnects == 1);
  reset();
  WiFi.modeValue = WIFI_STA;
  WiFi.statusValue = WL_CONNECTED;
  {
    rsssync::WifiSession wifi;
    assert(wifi.connect());
  }
  assert(WiFi.status() == WL_CONNECTED && WiFi.disconnects == 0 && WiFi.joins == 0);
  reset();
  WiFi.succeed = false;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(clockMs == 20000 && WiFi.modeValue == WIFI_OFF);
  reset();
  WIFI_STORE.saved = false;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(WiFi.joins == 0 && WiFi.disconnects == 0);
  reset();
  WiFi.modeValue = 2;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(WiFi.modeValue == 2 && WiFi.disconnects == 0);
  reset();
  WiFi.modeValue = WIFI_STA;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(WiFi.modeValue == WIFI_STA && WiFi.joins == 0);
  reset();
  devmode::enabled = true;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(WiFi.joins == 0);
  reset();
  {
    rsssync::WifiSession wifi;
    assert(wifi.connect());
    devmode::held = true;
  }
  assert(WiFi.status() == WL_CONNECTED && WiFi.disconnects == 0);
  reset();
  WiFi.hasIp = false;
  {
    rsssync::WifiSession wifi;
    assert(!wifi.connect());
  }
  assert(clockMs == 20000 && WiFi.modeValue == WIFI_OFF);
  puts("RSS Wi-Fi ownership: passed");
}
