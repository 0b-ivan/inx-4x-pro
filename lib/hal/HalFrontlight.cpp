/**
 * @file HalFrontlight.cpp
 * @brief X4 Pro frontlight implementation backed by FreeInk FrontlightManager.
 */

#include <HalFrontlight.h>
#include <Preferences.h>

namespace {
constexpr char kPrefsNamespace[] = "inx-light";
constexpr uint8_t kPrefsVersion = 1;
}

HalFrontlight frontlight;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  if (!manager.present()) {
    Serial.printf("[%lu] [LIGHT] no frontlight in active board profile\n", millis());
    return;
  }

  manager.begin();
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);

  if (!loadSettings()) {
    Serial.printf("[%lu] [LIGHT] no saved state; using brightness=%u%% warm=%u%% state=%s\n", millis(),
                  lastBrightness, manager.colorTemperature(), lit ? "on" : "off");
  }

  Serial.printf("[%lu] [LIGHT] ready brightness=%u%% warm=%u%% state=%s\n", millis(), lastBrightness,
                manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) {
    manager.setBrightness(lastBrightness);
  }
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (!manager.present() || on == lit) {
    return;
  }
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
  Serial.printf("[%lu] [LIGHT] %s brightness=%u%% warm=%u%%\n", millis(), lit ? "on" : "off", lastBrightness,
                manager.colorTemperature());
}

bool HalFrontlight::loadSettings() {
  if (!manager.present()) return false;

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return false;
  const uint8_t version = prefs.getUChar("version", 0);
  if (version != kPrefsVersion) {
    prefs.end();
    return false;
  }

  const uint8_t savedBrightness = prefs.getUChar("brightness", lastBrightness);
  const uint8_t savedWarmth = prefs.getUChar("warmth", manager.colorTemperature());
  const bool savedOn = prefs.getBool("on", lit);
  prefs.end();

  setBrightness(savedBrightness);
  setWarmth(savedWarmth);
  setOn(savedOn);
  Serial.printf("[%lu] [LIGHT] restored brightness=%u%% warm=%u%% state=%s\n", millis(), lastBrightness,
                manager.colorTemperature(), lit ? "on" : "off");
  return true;
}

bool HalFrontlight::saveSettings() const {
  if (!manager.present()) return false;

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return false;
  bool ok = true;
  ok = prefs.putUChar("version", kPrefsVersion) == sizeof(uint8_t) && ok;
  ok = prefs.putUChar("brightness", lastBrightness) == sizeof(uint8_t) && ok;
  ok = prefs.putUChar("warmth", manager.colorTemperature()) == sizeof(uint8_t) && ok;
  ok = prefs.putBool("on", lit) == sizeof(uint8_t) && ok;
  prefs.end();

  Serial.printf("[%lu] [LIGHT] saved brightness=%u%% warm=%u%% state=%s%s\n", millis(), lastBrightness,
                manager.colorTemperature(), lit ? "on" : "off", ok ? "" : " (write failed)");
  return ok;
}
