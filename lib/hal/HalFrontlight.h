#pragma once

/**
 * @file HalFrontlight.h
 * @brief Thin Inx HAL over FreeInk's board-profile-driven frontlight manager.
 */

#include <Arduino.h>
#include <FrontlightManager.h>

class HalFrontlight {
 public:
  void begin(uint8_t brightness = 60, uint8_t warmth = 50, bool on = false);

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }

  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);
  void toggle() {
    setOn(!lit);
    saveSettings();
  }

  bool loadSettings();
  bool saveSettings() const;

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  bool isOn() const { return lit; }

 private:
  FrontlightManager manager;
  uint8_t lastBrightness = 60;
  bool lit = false;
};

extern HalFrontlight frontlight;
