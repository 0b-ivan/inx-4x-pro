/**
 * @file HalFrontlight.cpp
 * @brief X4 Pro frontlight implementation backed by FreeInk FrontlightManager.
 */

#include <HalFrontlight.h>

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
