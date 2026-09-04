#include "RssFeedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "../../apps_local/Shelf.h"
#include "MappedInputManager.h"
#include "RssFeedStore.h"
#include "RssSettingsActivity.h"
#include "activities/browser/RssFeedBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

int RssFeedListActivity::getItemCount() const {
  const int count = static_cast<int>(RSS_STORE.getCount());
  return count + 1;  // Always include "Add feed"
}

void RssFeedListActivity::onEnter() {
  Activity::onEnter();
  RSS_STORE.loadFromFile();
  selectedIndex = 0;
  requestUpdate();
}

void RssFeedListActivity::onExit() { Activity::onExit(); }

void RssFeedListActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (pickerMode) {
      shelf::leave(renderer, mappedInput);
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = getItemCount();
  if (itemCount > 0 && mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageHeight = renderer.getScreenHeight();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    const int rowHeight = GUI.getMenuRowHeight(renderer);
    const int rowStep = rowHeight + metrics.menuSpacing;
    const int pageItems = std::max(1, contentHeight / std::max(1, rowStep));
    const int pageStart = (selectedIndex / pageItems) * pageItems;

    int touchedRow = -1;
    const auto rowTouch = mappedInput.rowTouch(touchedRow, contentTop, rowStep, std::min(pageItems, itemCount - pageStart),
                                               0, renderer.getScreenWidth(), rowHeight);
    if (rowTouch != MappedInputManager::RowTouch::None && touchedRow >= 0) {
      const int touchedIndex = pageStart + touchedRow;
      if (touchedIndex < itemCount) {
        if (rowTouch == MappedInputManager::RowTouch::Down) {
          if (selectedIndex != touchedIndex) {
            selectedIndex = touchedIndex;
            requestUpdate();
          }
        } else {
          selectedIndex = touchedIndex;
          handleSelection();
        }
      }
      return;
    }
  }

  if (itemCount > 0) {
    buttonNavigator.onNext([this, itemCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, itemCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
      requestUpdate();
    });
  }
}

void RssFeedListActivity::handleSelection() {
  const auto feedCount = static_cast<int>(RSS_STORE.getCount());

  if (selectedIndex < feedCount) {
    if (pickerMode) {
      const auto* feed = RSS_STORE.getFeed(static_cast<size_t>(selectedIndex));
      if (feed) {
        startActivityForResult(std::make_unique<RssFeedBrowserActivity>(renderer, mappedInput, *feed),
                               [this](const ActivityResult&) { requestUpdate(); });
      }
      return;
    }

    startActivityForResult(std::make_unique<RssSettingsActivity>(renderer, mappedInput, selectedIndex),
                           [this](const ActivityResult&) {
                             RSS_STORE.loadFromFile();
                             selectedIndex = 0;
                             requestUpdate();
                           });
    return;
  }

  startActivityForResult(std::make_unique<RssSettingsActivity>(renderer, mappedInput, -1),
                         [this](const ActivityResult&) {
                           RSS_STORE.loadFromFile();
                           selectedIndex = 0;
                           requestUpdate();
                         });
}

void RssFeedListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 pickerMode ? tr(STR_RSS_READER) : tr(STR_RSS_FEEDS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = getItemCount();

  const auto& feeds = RSS_STORE.getFeeds();
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
      [&feeds](int index) {
        if (index < static_cast<int>(feeds.size())) {
          const auto& feed = feeds[index];
          return feed.name.empty() ? feed.url : feed.name;
        }
        return std::string(I18N.get(StrId::STR_ADD_FEED));
      },
      [&feeds](int index) {
        if (index < static_cast<int>(feeds.size()) && !feeds[index].name.empty()) return feeds[index].url;
        return std::string("");
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
