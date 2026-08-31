#include "EpubActivity.h"

#include "system/X4ProQuickPrefs.h"

bool EpubActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) return ActivityWithSubactivity::handleTouchTap(x, y);
  if (!X4ProQuickPrefs::readerTouchEnabled()) return false;

  if (annUi_.isActive()) return annUi_.handleTouchTap(*this, x, y);

  // Existing modal reader UIs own their input while visible. Returning false
  // keeps the latched tap available to legacy drawer/overlay input handling.
  if (dictUi_.isActive() || orientationPicker_.isActive() || presetPicker_.isActive() ||
      quickActionsUi_.isActive() || menuDrawerVisible || settingsDrawerVisible || isDoingSomethingHeavy || !section ||
      !epub) {
    return false;
  }

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  if (x < 0 || x >= width || y < 0 || y >= height) return false;

  if (x < width / 3) {
    endPageTimer();
    pageTurn(false);
    lastAutoPageTurnTime = millis();
    return true;
  }

  if (x >= (width * 2) / 3) {
    endPageTimer();
    pageTurn(true);
    lastAutoPageTurnTime = millis();
    return true;
  }

  pauseReadingStats();
  toggleMenuDrawer();
  return true;
}
