#include "RadioManager.h"

#if !defined(SIMULATOR)
#include <BLEDevice.h>
#include <WiFi.h>

#include "DevMode.h"
#endif

RadioManager RADIO;

void RadioManager::ensureWifi() {
#if !defined(SIMULATOR)
  if (mode_ == Mode::Ble) {
    BLEDevice::deinit(true);
  }
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
#endif
  mode_ = Mode::Wifi;
}

void RadioManager::ensureBle() {
  if (mode_ == Mode::Ble) return;
#if !defined(SIMULATOR)
  BLEDevice::init("CrossPlay");
#endif
  mode_ = Mode::Ble;
}

void RadioManager::shutdown() {
#if !defined(SIMULATOR)
  if (mode_ == Mode::Ble) {
    BLEDevice::deinit(true);
  } else if (mode_ == Mode::Wifi && !devmode::inhibitsSleep()) {
    // A paused Developer Mode must be able to reclaim its association.
    WiFi.mode(WIFI_OFF);
  }
#endif
  mode_ = Mode::None;
}
