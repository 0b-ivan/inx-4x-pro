#include "EpubActivity.h"

bool EpubActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) return ActivityWithSubactivity::handleTouchTap(x, y);

  // Existing modal reader UIs own their input while visible. Returning false
  // keeps the latched tap available to legacy drawer/overlay input handling.
  if (annUi_.isActive() || dictUi_.isActive() || orientationPicker_.isActive() || presetPicker_.isActive() ||
      quickActionsUi_.isActive() || menuDrawerVisible || settingsDrawerVisible || isDoingSomethingHeavy || !section ||
      !epub) {
    return false;
  }

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  if (x < 0 || x >= width || y < 0 || y >= height) return false;

  // CrossPoint-style simple reader zones: outer thirds turn pages; the centre
  // opens the reader menu. The global top-edge pull-down is handled before this
  // method, so it never collides with the quick-settings gesture.
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
