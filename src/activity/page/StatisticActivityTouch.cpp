#include "StatisticActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "system/MappedInputManager.h"
#include "system/UiTheme.h"

bool StatisticActivity::handleTouchTap(const int x, const int y) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  if (x < 0 || x >= width || y < 0 || y >= height) return false;

  // Classic bottom button hints are visible controls: Recent and Refresh.
  if (!INX_THEME.mainTabsAtBottom() && y >= height - 40) {
    const auto labels = mappedInput.mapLabels("\xC2\xAB Recent", "Refresh", "", "");
    const char* slots[] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
    constexpr int positions[] = {25, 130, 245, 350};
    constexpr int buttonWidth = 106;
    for (int slot = 0; slot < 4; ++slot) {
      if (x < positions[slot] || x >= positions[slot] + buttonWidth || !slots[slot] || slots[slot][0] == '\0') {
        continue;
      }
      const std::string label = slots[slot];
      if (label == "\xC2\xAB Recent") {
        if (onGoToRecent) onGoToRecent();
        return true;
      }
      if (label == "Refresh") {
        loadStats();
        updateRequired = true;
        return true;
      }
    }
  }

  // Edge taps mirror Up/Down navigation without stealing the central stats UI.
  const int edgeWidth = std::max(48, width / 6);
  if (x >= edgeWidth && x < width - edgeWidth) return false;

  const int totalViews = 1 + static_cast<int>(allBooksStats.size());
  if (totalViews <= 1) return false;

  if (x < edgeWidth) {
    viewIndex = (viewIndex + totalViews - 1) % totalViews;
  } else {
    viewIndex = (viewIndex + 1) % totalViews;
  }
  if (viewIndex > 0) ensureBookStatsLoaded(viewIndex - 1);
  updateRequired = true;
  return true;
}
