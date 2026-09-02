/**
 * @file BootActivity.cpp
 * @brief Definitions for BootActivity.
 */

#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#ifndef SIMULATOR
#include <Preferences.h>
#endif

#include "KOReaderCredentialStore.h"
#include "images/CorgiWhite.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

extern void onGoToRecent();
extern void onGoToReader(const std::string&);
extern HalDisplay display;
extern HalGPIO gpio;
extern MappedInputManager mappedInputManager;
extern GfxRenderer renderer;
extern Activity* currentActivity;

BootActivity::BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager)
    : Activity("BootActivity", renderer, inputManager) {}

/**
 * @brief Initializes the boot activity when it becomes active.
 */
void BootActivity::onEnter() {
  Activity::onEnter();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

  if (SdMan.ready() && SdMan.exists(KOReaderCredentialStore::SYSTEM_SETTINGS_PATH)) {
    (void)KOREADER_STORE.loadFromFile();
  }

#ifndef SIMULATOR
  Preferences otaPrefs;
  if (otaPrefs.begin("inx-ota", false)) {
    showChangelog = otaPrefs.getString("changelog", "").length() > 0;
    otaPrefs.end();
  }
#endif

  if (showChangelog) {
    renderChangelog();
  }

  bootComplete = true;
}

void BootActivity::renderChangelog() {
  renderer.clearScreen();
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 110, "UPDATE INSTALLED", true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_14_FONT_ID, 170, INX_VERSION, true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 260, "Footer buttons removed", true, EpdFontFamily::REGULAR);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 300, "Swipe right to go back", true, EpdFontFamily::REGULAR);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 340, "OTA update improvements", true, EpdFontFamily::REGULAR);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 470, "Press confirm or swipe right to continue", true,
                         EpdFontFamily::REGULAR);
  renderer.displayBuffer();
}

/**
 * @brief Main update loop for the boot activity.
 */
void BootActivity::loop() {
  if (bootComplete) {
    if (showChangelog) {
      if (!mappedInput.wasPressed(MappedInputManager::Button::Confirm) &&
          !mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        return;
      }
      showChangelog = false;
#ifndef SIMULATOR
      Preferences otaPrefs;
      if (otaPrefs.begin("inx-ota", false)) {
        otaPrefs.remove("changelog");
        otaPrefs.end();
      }
#endif
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
    return;
  }
}
