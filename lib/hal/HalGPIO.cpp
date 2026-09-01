/**
 * @file HalGPIO.cpp
 * @brief Xteink X4 Pro hardware abstraction backed by FreeInk BoardConfig.
 */

#include <BoardConfig.h>
#include <HalGPIO.h>
#include <PowerManager.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <ctime>

namespace {
void addVirtualClick(uint8_t button, uint8_t& pressed, uint8_t& released) {
  const uint8_t mask = static_cast<uint8_t>(1u << button);
  pressed |= mask;
  released |= mask;
}
}  // namespace

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
  rtcAvailable = rtc.begin();
}

void HalGPIO::update() {
  virtualPressedEvents = 0;
  virtualReleasedEvents = 0;

  inputMgr.update();

  // CrossPoint keeps screen touch as raw touch all the way to its mapped-input
  // layer. Do the same here: arbitrary screen positions must never turn into
  // directional button presses inside the HAL. The dedicated capacitive Home
  // key is different hardware and intentionally retains Inx's Back action.
  if (inputMgr.wasHomeKeyTapped()) {
    addVirtualClick(BTN_BACK, virtualPressedEvents, virtualReleasedEvents);
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

unsigned long HalGPIO::getHeldTime() const {
  // CrossPoint's X4 Pro power-double-click path evaluates the dedicated power
  // button hold duration. Inx historically exposes only one generic held-time
  // accessor, so on the power release frame return FreeInk's latched power hold
  // value instead of the last arbitrary button/touch duration. This keeps all
  // existing Inx callers unchanged while matching the X4 Pro input contract.
  if (inputMgr.wasReleased(BTN_POWER)) {
    return inputMgr.getPowerButtonHeldTime();
  }
  return inputMgr.getHeldTime();
}

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }

bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return inputMgr.wasTouchLongPress(nx, ny); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

HalGPIO::MotionGesture HalGPIO::readMotionGesture(uint8_t, uint8_t, uint8_t) { return MotionGesture::None; }

void HalGPIO::startDeepSleep() {
  // Match CrossPoint's X4 Pro sleep sequence: GPIO isolation follows below, so
  // every master power latch must be driven HIGH and explicitly held first.
  // Merely calling BoardConfig::holdPowerRails() during boot is insufficient:
  // it asserts the level but intentionally does not arm a deep-sleep hold.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    const auto gpioPin = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpioPin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(gpioPin);
  }

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

bool HalGPIO::readDateTime(DateTime& out, const int timeZoneOffsetMinutes) const {
  if (!rtcAvailable) return false;
  Rtc::DateTime raw;
  if (!rtc.now(raw)) return false;

  struct tm utc{};
  utc.tm_year = raw.year - 1900;
  utc.tm_mon = raw.month - 1;
  utc.tm_mday = raw.day;
  utc.tm_hour = raw.hour;
  utc.tm_min = raw.minute;
  utc.tm_sec = raw.second;
  // FreeInk uses the same mktime/localtime round-trip for calendar-correct
  // adjustment. The firmware keeps the process timezone at UTC.
  time_t epoch = mktime(&utc) + static_cast<time_t>(timeZoneOffsetMinutes) * 60;
  struct tm shown{};
  if (gmtime_r(&epoch, &shown) == nullptr) return false;
  out.year = static_cast<uint16_t>(shown.tm_year + 1900);
  out.month = static_cast<uint8_t>(shown.tm_mon + 1);
  out.day = static_cast<uint8_t>(shown.tm_mday);
  out.hour = static_cast<uint8_t>(shown.tm_hour);
  out.minute = static_cast<uint8_t>(shown.tm_min);
  out.second = static_cast<uint8_t>(shown.tm_sec);
  out.weekday = static_cast<uint8_t>(shown.tm_wday);
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& value) const {
  if (!rtcAvailable) return false;
  const Rtc::DateTime raw{value.year,
                          value.month,
                          value.day,
                          value.hour,
                          value.minute,
                          value.second,
                          static_cast<uint8_t>(value.weekday % 7)};
  return rtc.set(raw);
}

bool HalGPIO::syncRtcFromSystemTime() const {
  const time_t now = time(nullptr);
  if (!rtcAvailable || now < 1704067200) return false;
  struct tm utc{};
  if (gmtime_r(&now, &utc) == nullptr) return false;
  const Rtc::DateTime value{static_cast<uint16_t>(utc.tm_year + 1900), static_cast<uint8_t>(utc.tm_mon + 1),
                            static_cast<uint8_t>(utc.tm_mday),         static_cast<uint8_t>(utc.tm_hour),
                            static_cast<uint8_t>(utc.tm_min),          static_cast<uint8_t>(utc.tm_sec),
                            static_cast<uint8_t>(utc.tm_wday)};
  return rtc.set(value);
}

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