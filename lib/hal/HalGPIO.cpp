/**
 * @file HalGPIO.cpp
 * @brief Xteink X4 Pro hardware abstraction backed by FreeInk BoardConfig.
 */

#include <HalGPIO.h>

#include <BoardConfig.h>
#include <PowerManager.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>
#include <esp_system.h>

void HalGPIO::begin() {
  // Match FreeInk/CrossPoint's X4 Pro bring-up order. Some X4 Pro peripherals
  // sit behind switched rails; assert the board-profile-defined boot levels
  // before touching input, SD or display buses.
  BoardConfig::holdPowerRails();

  // X4 Pro production batches can use different 800x480 panel controllers.
  // Resolve the controller before display.begin(); FreeInk then selects the
  // matching driver while retaining the same board pinout.
  freeink::applyXteinkDisplayController();

  // InputManager resolves the X4 Pro's digital buttons and GT911 controller from
  // BoardConfig. Do not initialize SPI or probe legacy X3/C3 pins here.
  inputMgr.begin();
}

void HalGPIO::update() {
  virtualPressedEvents = 0;
  virtualReleasedEvents = 0;

  inputMgr.update();

  // The X4 Pro profile deliberately leaves back/confirm GPIOs unassigned:
  // Up/Down are the two side keys, while GT911 + the capacitive Home key supply
  // the missing actions. Bridge those actions into Inx's existing button model
  // without making individual activities touch-aware yet.
  float nx = 0.0f;
  float ny = 0.0f;
  if (inputMgr.wasTouchTap(nx, ny)) {
    const uint8_t mask = static_cast<uint8_t>(1u << BTN_CONFIRM);
    virtualPressedEvents |= mask;
    virtualReleasedEvents |= mask;
  }

  if (inputMgr.wasHomeKeyTapped()) {
    const uint8_t mask = static_cast<uint8_t>(1u << BTN_BACK);
    virtualPressedEvents |= mask;
    virtualReleasedEvents |= mask;
  }
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  const uint8_t mask = static_cast<uint8_t>(1u << buttonIndex);
  return inputMgr.wasPressed(buttonIndex) || (virtualPressedEvents & mask) != 0;
}

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed() || virtualPressedEvents != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  const uint8_t mask = static_cast<uint8_t>(1u << buttonIndex);
  return inputMgr.wasReleased(buttonIndex) || (virtualReleasedEvents & mask) != 0;
}

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased() || virtualReleasedEvents != 0; }

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
