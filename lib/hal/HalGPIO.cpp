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

#include <cmath>

namespace {
struct LogicalTouchPoint {
  float x;
  float y;
};

void addVirtualClick(uint8_t button, uint8_t& pressed, uint8_t& released) {
  const uint8_t mask = static_cast<uint8_t>(1u << button);
  pressed |= mask;
  released |= mask;
}

// FreeInk normalizes GT911 coordinates into the panel's native landscape frame.
// Inx renders its default UI in GfxRenderer::Portrait, whose pixel transform is:
//   panelX = logicalY
//   panelY = panelHeight - 1 - logicalX
// The normalized inverse is therefore logicalX = 1 - panelY, logicalY = panelX.
// Keep this mapping next to the X4 Pro compatibility bridge until touch becomes a
// first-class app input and can share GfxRenderer's orientation directly.
LogicalTouchPoint panelToPortrait(const float panelX, const float panelY) { return {1.0f - panelY, panelX}; }

uint8_t buttonForPortraitTap(const LogicalTouchPoint point) {
  // The bottom row is already drawn by Inx as two soft-key hints (Menu/Open).
  // Make those labels actual touch targets on X4 Pro.
  if (point.y >= 0.88f) {
    return point.x < 0.50f ? HalGPIO::BTN_BACK : HalGPIO::BTN_CONFIRM;
  }

  // The rest of the legacy button-oriented UI gets large, forgiving navigation
  // zones. This intentionally does not pretend every legacy widget has native
  // hit-testing yet; it gives every required logical action a usable touch path.
  if (point.x <= 0.18f) {
    return HalGPIO::BTN_LEFT;
  }
  if (point.x >= 0.82f) {
    return HalGPIO::BTN_RIGHT;
  }
  if (point.y <= 0.36f) {
    return HalGPIO::BTN_UP;
  }
  if (point.y >= 0.64f) {
    return HalGPIO::BTN_DOWN;
  }
  return HalGPIO::BTN_CONFIRM;
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
}

void HalGPIO::update() {
  virtualPressedEvents = 0;
  virtualReleasedEvents = 0;

  inputMgr.update();

  // X4 Pro has physical Up/Down side keys but the original Inx UI expects seven
  // logical buttons. FreeInk deliberately returns GT911 taps as normalized
  // panel-native coordinates, so translate them into the Portrait UI frame and
  // bridge large touch zones into the existing button model.
  float nx = 0.0f;
  float ny = 0.0f;
  if (inputMgr.wasTouchTap(nx, ny)) {
    const LogicalTouchPoint point = panelToPortrait(nx, ny);
    const uint8_t button = buttonForPortraitTap(point);
    Serial.printf("[%lu] [X4PRO INPUT] tap panel=(%.3f,%.3f) portrait=(%.3f,%.3f) -> button=%u\n", millis(), nx, ny,
                  point.x, point.y, button);
    addVirtualClick(button, virtualPressedEvents, virtualReleasedEvents);
  }

  // The capacitive key below the display acts as Inx's Back/Menu action. On the
  // home screen that opens the same menu advertised by the bottom-left soft key;
  // on deeper screens it backs out through the existing activity stack.
  if (inputMgr.wasHomeKeyTapped()) {
    addVirtualClick(BTN_BACK, virtualPressedEvents, virtualReleasedEvents);
  }

  // Swipes use content-navigation semantics: swipe left/right advances to the
  // neighboring tab/page; swipe up/down advances to the next/previous list item.
  // Convert both endpoints to the same Portrait frame before deciding direction.
  float nxStart = 0.0f;
  float nyStart = 0.0f;
  float nxEnd = 0.0f;
  float nyEnd = 0.0f;
  if (inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd)) {
    const LogicalTouchPoint start = panelToPortrait(nxStart, nyStart);
    const LogicalTouchPoint end = panelToPortrait(nxEnd, nyEnd);
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;

    uint8_t button = BTN_CONFIRM;
    if (std::fabs(dx) > std::fabs(dy)) {
      button = dx < 0.0f ? BTN_RIGHT : BTN_LEFT;
    } else {
      button = dy < 0.0f ? BTN_DOWN : BTN_UP;
    }

    Serial.printf("[%lu] [X4PRO INPUT] swipe portrait=(%.3f,%.3f)->(%.3f,%.3f) dx=%.3f dy=%.3f -> button=%u\n",
                  millis(), start.x, start.y, end.x, end.y, dx, dy, button);
    addVirtualClick(button, virtualPressedEvents, virtualReleasedEvents);
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