/**
 * @file BootActivity.cpp
 * @brief Definitions for BootActivity.
 */

#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#ifndef SIMULATOR
#include <esp_sleep.h>
#endif

#include "KOReaderCredentialStore.h"
#include "images/CorgiWhite.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"

extern void onGoToRecent();
extern void onGoToReader(const std::string&);
extern void enterDeepSleep();
extern HalDisplay display;
extern HalGPIO gpio;
extern MappedInputManager mappedInputManager;
extern GfxRenderer renderer;
extern Activity* currentActivity;

BootActivity::BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager)
    : Activity("BootActivity", renderer, inputManager) {}

void BootActivity::onEnter() {
  Activity::onEnter();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (SdMan.ready() && SdMan.exists(KOReaderCredentialStore::SYSTEM_SETTINGS_PATH)) {
    (void)KOREADER_STORE.loadFromFile();
  }

#ifndef SIMULATOR
  deskCalendarTimerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER &&
                          SETTINGS.sleepScreen == SystemSetting::DATETIME &&
                          SETTINGS.sleepClockStyle == SystemSetting::CLOCK_HORIZONTAL_CARD;
#endif

  bootComplete = true;
}

void BootActivity::loop() {
  if (!bootComplete) return;

  // A desk-calendar timer wake is maintenance, not a user boot. Re-enter the
  // sleep path immediately; SleepActivity performs a due CalDAV refresh and
  // redraws the cached appointments before deep sleep is armed again.
  if (deskCalendarTimerWake) {
    deskCalendarTimerWake = false;
    enterDeepSleep();
    return;
  }

  if (APP_STATE.lastRead.empty() || SETTINGS.bootSetting == SystemSetting::HOME_PAGE) {
    onGoToRecent();
  } else {
    const auto path = APP_STATE.lastRead;
    APP_STATE.lastRead = "";
    APP_STATE.saveToFile();
    onGoToReader(path);
  }
}
