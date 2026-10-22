#pragma once

#include <cstdint>

// Ported from biscuit_x3/src/util/RadioManager.h.
// CrossPlay's Developer Mode must yield while an app owns the radio.
class RadioManager {
 public:
  enum class RadioState { OFF, WIFI, BLE };

  static RadioManager& getInstance() {
    static RadioManager instance;
    return instance;
  }

  bool ensureWifi();
  bool ensureBle();
  void shutdown();

  RadioState getState() const { return state; }

  bool isDisclaimerAcknowledged() const;
  void setDisclaimerAcknowledged();

 private:
  RadioManager() = default;
  RadioState state = RadioState::OFF;

  void deinitWifi();
  void deinitBle();
};

#define RADIO RadioManager::getInstance()
