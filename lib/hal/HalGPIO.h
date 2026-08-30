#pragma once

/**
 * @file HalGPIO.h
 * @brief Xteink X4 Pro hardware input/power abstraction.
 *
 * The X4 Pro port deliberately contains no hard-coded display, button, battery
 * or wake GPIOs. FreeInk BoardConfig is the single source of truth for hardware
 * wiring so production-batch differences stay inside the validated SDK profile.
 */

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

 public:
  enum class DeviceType : uint8_t { X4, X3 };

  struct DateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

 private:
  mutable int batteryCachedPercent = 0;
  mutable unsigned long batteryLastPollMs = 0;

  // X4 Pro has only Up/Down/Power as discrete buttons. FreeInk intentionally
  // leaves GT911 taps to the consumer. Inx is button-oriented, so expose a
  // short virtual click pulse: screen tap -> Confirm, capacitive Home -> Back.
  // A click is represented as both press + release in one update, matching
  // FreeInk's own synthesized short-click behavior for other board profiles.
  uint8_t virtualPressedEvents = 0;
  uint8_t virtualReleasedEvents = 0;

 public:
  static constexpr unsigned long BATTERY_POLL_MS = 1500;
  enum class MotionGesture : uint8_t { None, Previous, Next };

  HalGPIO() = default;

  // The fork is intentionally X4-Pro-only. Keep these compatibility helpers so
  // the existing Inx application code can remain unchanged during the port.
  bool deviceIsX3() const { return false; }
  bool deviceIsX4() const { return true; }

  void begin();

  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity);

  void startDeepSleep();

  int getBatteryPercentage() const;
  bool isUsbConnected() const;

  // X4 Pro has its own RTC support in FreeInk, but Inx does not consume that HAL
  // yet. These compatibility methods stay disabled until the clock layer is
  // ported separately.
  bool readDateTime(DateTime& outDateTime) const;
  bool writeDateTime(const DateTime& dateTime) const;
  bool syncRtcFromSystemTime() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };
  WakeupReason getWakeupReason() const;

  static constexpr uint8_t BTN_BACK = InputManager::BTN_BACK;
  static constexpr uint8_t BTN_CONFIRM = InputManager::BTN_CONFIRM;
  static constexpr uint8_t BTN_LEFT = InputManager::BTN_LEFT;
  static constexpr uint8_t BTN_RIGHT = InputManager::BTN_RIGHT;
  static constexpr uint8_t BTN_UP = InputManager::BTN_UP;
  static constexpr uint8_t BTN_DOWN = InputManager::BTN_DOWN;
  static constexpr uint8_t BTN_POWER = InputManager::BTN_POWER;
};

extern HalGPIO gpio;
