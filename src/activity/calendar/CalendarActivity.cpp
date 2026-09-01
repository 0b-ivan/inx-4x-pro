#include "CalendarActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include "state/SystemSetting.h"
#include "system/DeskCalendarRenderer.h"
#include "system/MappedInputManager.h"
#include "system/SleepClockRenderer.h"

extern HalGPIO gpio;

void CalendarActivity::onEnter() {
  Activity::onEnter();
  render();
}

void CalendarActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack_) onBack_();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    render();
  }
}

bool CalendarActivity::handleTouchTap(const int x, const int y) {
  if (x < 0 || x >= renderer.getScreenWidth() || y < 0 || y >= renderer.getScreenHeight()) return false;
  render();
  return true;
}

void CalendarActivity::render() {
  SleepClockRenderer::DateTimeView view;
  bool hasClock = false;
#ifndef SIMULATOR
  HalGPIO::DateTime dateTime;
  hasClock = gpio.readDateTime(dateTime, SETTINGS.getTimeZoneOffsetMinutes());
  if (hasClock) {
    view.year = dateTime.year;
    view.month = dateTime.month;
    view.day = dateTime.day;
    view.hour = dateTime.hour;
    view.minute = dateTime.minute;
    view.weekday = dateTime.weekday == 0 ? 7 : dateTime.weekday;
  }
#endif

  renderer.clearScreen(0xff);
  if (hasClock) {
    DeskCalendarRenderer::render(renderer, view, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  } else {
    SleepClockRenderer::render(renderer, SystemSetting::CLOCK_HORIZONTAL_CARD, view, false, 0, 0,
                               renderer.getScreenWidth(), renderer.getScreenHeight());
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
