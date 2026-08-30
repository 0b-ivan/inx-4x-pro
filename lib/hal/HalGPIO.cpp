/**
 * @file HalGPIO.cpp
 * @brief Xteink X4 Pro hardware abstraction backed by FreeInk BoardConfig.
 */

#include <HalGPIO.h>

#include <BoardConfig.h>
#include <PowerManager.h>
#include <esp_sleep.h>
#include <esp_system.h>

void HalGPIO::begin() {
  // InputManager resolves the X4 Pro's digital buttons and GT911 controller from
  // BoardConfig. Do not initialize SPI or probe legacy X3/C3 pins here.
  inputMgr.begin();
}

void HalGPIO::update() { inputMgr.update(); }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

HalGPIO::MotionGesture HalGPIO::readMotionGesture(uint8_t, uint8_t, uint8_t) { return MotionGesture::None; }

void HalGPIO::startDeepSleep() {
  // The X4 Pro is ESP32-S3. FreeInk selects the correct ext1 wake source and
  // powers down board rails using the active BoardConfig profile.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

int HalGPIO::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (batteryLastPollMs != 0 && (now - batteryLastPollMs) < BATTERY_POLL_MS) {
    return batteryCachedPercent;
  }

  static const BatteryMonitor battery;
  uint16_t percent = 0;
  if (battery.readPercentageChecked(percent) && percent <= 100) {
    batteryCachedPercent = static_cast<int>(percent);
  }
  batteryLastPollMs = now;
  return batteryCachedPercent;
}

bool HalGPIO::isUsbConnected() const {
  // Prefer a real USB/VBUS detect line when the board profile exposes one.
  if (BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }

  // Fallback to charging telemetry. This can report false at charge termination,
  // but it never requires poking an unverified X4/C3 GPIO.
  static const BatteryMonitor battery;
  return battery.isCharging();
}

bool HalGPIO::readDateTime(DateTime&) const { return false; }

bool HalGPIO::writeDateTime(const DateTime&) const { return false; }

bool HalGPIO::syncRtcFromSystemTime() const { return false; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }

  // Xteink's power topology allows a battery-only cold boot from the held power
  // button. This classification is used only by Inx's existing boot UX; it does
  // not write flash or alter boot configuration.
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }

  return WakeupReason::Other;
}
