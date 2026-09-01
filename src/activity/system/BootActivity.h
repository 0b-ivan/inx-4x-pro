#pragma once

/**
 * @file BootActivity.h
 * @brief Public interface and types for BootActivity.
 */

#include "../Activity.h"
#include "system/MappedInputManager.h"

/**
 * @brief Boot activity that displays splash screen and loads system components.
 *
 * Loads system settings, application state, recent books, and book state while
 * showing a short progress indication. When KOReader sync settings exist on SD,
 * they are loaded during this phase. After initialization, transitions either to
 * the recent books view or directly to the last opened book per boot settings.
 */
class BootActivity : public Activity {
 public:
  BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager);
  void onEnter() override;
  void loop() override;

 private:
  int bootProgress = 0;
  bool bootComplete = false;
  bool deskCalendarTimerWake = false;
};
