#include "RadioManager.h"

// The source implementation lives in biscuit_x3/src/util/RadioManager.cpp.
// ESP-specific calls are excluded from the desktop simulator build.
#if !defined(SIMULATOR)
#include <BLEDevice.h>
#include <Logging.h>
#include <Preferences.h>
#include <WiFi.h>

#include "DevMode.h"
#endif

bool RadioManager::ensureWifi() {
  if (state == RadioState::WIFI) return true;
#if !defined(SIMULATOR)
  if (state == RadioState::BLE) deinitBle();
  WiFi.mode(WIFI_STA);
#endif
  state = RadioState::WIFI;
  return true;
}

bool RadioManager::ensureBle() {
  if (state == RadioState::BLE) return true;
#if !defined(SIMULATOR)
  if (state == RadioState::WIFI) deinitWifi();
  BLEDevice::init("CrossPlay");
#endif
  state = RadioState::BLE;
  return true;
}

void RadioManager::shutdown() {
#if !defined(SIMULATOR)
  if (state == RadioState::WIFI) deinitWifi();
  if (state == RadioState::BLE) deinitBle();
#endif
  state = RadioState::OFF;
}

void RadioManager::deinitWifi() {
#if !defined(SIMULATOR)
  // Developer Mode can be yielding to an app while still owning the Wi-Fi
  // association. Never tear down that association on an app's exit.
  if (!devmode::inhibitsSleep()) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
#endif
}

void RadioManager::deinitBle() {
#if !defined(SIMULATOR)
  BLEDevice::deinit(false);
#endif
}

bool RadioManager::isDisclaimerAcknowledged() const {
#if !defined(SIMULATOR)
  Preferences prefs;
  if (!prefs.begin("crossplay", true)) return false;
  const bool acknowledged = prefs.getBool("disc_ack", false);
  prefs.end();
  return acknowledged;
#else
  return false;
#endif
}

void RadioManager::setDisclaimerAcknowledged() {
#if !defined(SIMULATOR)
  Preferences prefs;
  if (!prefs.begin("crossplay", false)) return;
  prefs.putBool("disc_ack", true);
  prefs.end();
#endif
}
