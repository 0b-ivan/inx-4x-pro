#include "RecentActivity.h"

#include <algorithm>

#include "state/SystemSetting.h"
#include "system/UiTheme.h"

namespace {
inline bool inRect(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
}  // namespace

bool RecentActivity::handleTouchTap(const int x, const int y) {
  if (recentBooks.empty() || removeConfirmOpen_ || (homeMenuDrawer_ && homeMenuDrawer_->visible())) return false;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  if (x < 0 || x >= screenW || y < 0 || y >= screenH) return false;

  auto openIndex = [this](const int index) {
    if (index < 0 || index >= static_cast<int>(recentBooks.size())) return false;
    selectorIndex = index;
    const RecentBook& book = recentBooks[static_cast<size_t>(index)];
    return openBookPath(book.path, book.title, book.author, true);
  };

  if (currentViewMode == ViewMode::Flow) {
    const int carouselY = mainContentTop() + 5;
    constexpr int carouselH = 340;
    if (y < carouselY || y >= carouselY + carouselH) return false;

    if (x < screenW / 3) {
      if (selectorIndex > 0) {
        --selectorIndex;
        updateRequired = true;
      }
      return true;
    }
    if (x >= (screenW * 2) / 3) {
      if (selectorIndex + 1 < static_cast<int>(recentBooks.size())) {
        ++selectorIndex;
        updateRequired = true;
      }
      return true;
    }
    return openIndex(selectorIndex);
  }

  if (currentViewMode == ViewMode::Cover) {
    const int top = mainContentTop();
    const int bottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 36;
    if (y >= top && y < bottom) return openIndex(selectorIndex);
    return false;
  }

  if (currentViewMode == ViewMode::List) {
    const int startY = recentListPaintStartY();
    const int hintReserve = INX_THEME.mainTabsAtBottom() ? INX_THEME.mainTabBarHeight() : 54;
    constexpr int listPadY = 6;
    const int listTop = startY + listPadY;
    const int contentBottom = screenH - hintReserve - listPadY;
    const int contentH = std::max(1, contentBottom - listTop);
    if (y < listTop || y >= contentBottom) return false;

    int slot = ((y - listTop) * LIST_VISIBLE_ITEMS) / contentH;
    slot = std::max(0, std::min(LIST_VISIBLE_ITEMS - 1, slot));
    return openIndex(scrollOffset + slot);
  }

  if (currentViewMode == ViewMode::Grid) {
    constexpr int spacing = 8;
    const int startY = recentGridPaintStartY();
    const int visibleRows = std::max(1, getVisibleRows());
    const int contentBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 54;
    const int availableWidth = screenW - (GRID_COLS + 1) * spacing;
    const int containerWidth = std::max(1, availableWidth / GRID_COLS);
    const int availableHeight = contentBottom - startY - spacing * 2;
    const int containerHeight = std::max(1, (availableHeight / visibleRows) - spacing);

    for (int row = 0; row < visibleRows; ++row) {
      for (int col = 0; col < GRID_COLS; ++col) {
        const int rx = spacing + col * (containerWidth + spacing);
        const int ry = startY + spacing + row * (containerHeight + spacing);
        if (!inRect(x, y, rx, ry, containerWidth, containerHeight)) continue;
        const int index = (scrollOffset + row) * GRID_COLS + col;
        return openIndex(index);
      }
    }
    return false;
  }

  if (currentViewMode == ViewMode::Icons) {
    constexpr int cols = ICON_COLS;
    constexpr int rows = ICON_ROWS;
    constexpr int gap = 8;
    constexpr int marginX = 10;
    constexpr int marginY = 8;
    const int startY = recentIconsPaintStartY();
    const int contentBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) : screenH - 54;
    const int availW = std::max(1, screenW - marginX * 2);
    const int availH = std::max(1, contentBottom - startY - marginY * 2);
    const int frameW = std::max(40, (availW - (cols - 1) * gap) / cols);
    const int frameH = std::max(40, (availH - (rows - 1) * gap) / rows);
    const int blockW = cols * frameW + (cols - 1) * gap;
    const int blockH = rows * frameH + (rows - 1) * gap;
    const int row0X = marginX + std::max(0, (availW - blockW) / 2);
    const int blockTop = startY + marginY + std::max(0, (availH - blockH) / 2);

    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < cols; ++col) {
        const int rx = row0X + col * (frameW + gap);
        const int ry = blockTop + row * (frameH + gap);
        if (!inRect(x, y, rx, ry, frameW, frameH)) continue;
        const int index = (scrollOffset + row) * cols + col;
        return openIndex(index);
      }
    }
  }

  return false;
}
