/**
 * @file SyncActivity.cpp
 * @brief Definitions for SyncActivity.
 */

#include "../page/SyncActivity.h"

#include <GfxRenderer.h>
#ifndef SIMULATOR
#include <HalFrontlight.h>
#endif

#include "activity/network/BackupRestoreActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/tarot/TarotActivity.h"
#include "activity/settings/DictionaryPickerActivity.h"
#include "activity/settings/KOReaderSettingsActivity.h"
#include "activity/settings/OtaUpdateActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int MENU_ITEM_COUNT = 10;
const char* MENU_ITEMS[MENU_ITEM_COUNT] = {"Manage via wifi",   "Connect to calibre", "Create hotspot",
                                           "OPDS Browser",      "Backup and restore", "KOReader Sync",
                                           "Check for updates", "Choose dictionary",  "Tarot",
                                           "Frontlight"};
// Ten rows still fit the X4 Pro portrait content area while retaining a large
// finger-sized touch target. The previous theme row height only fit nine rows.
constexpr int LIST_ITEM_HEIGHT = 58;
}  // namespace

/**
 * Lifecycle hook called when entering the activity.
 */
void SyncActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;

  render();
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

void SyncActivity::activateSelected() {
  NetworkMode mode = NetworkMode::JOIN_NETWORK;

  if (selectedIndex == 1) {
    mode = NetworkMode::CONNECT_CALIBRE;
  }

  if (selectedIndex == 2) {
    mode = NetworkMode::CREATE_HOTSPOT;
  }

  if (selectedIndex == 3) {
    mode = NetworkMode::OPDS_BROWSER;
  }

  if (selectedIndex == 4) {
    enterNewActivity(new BackupRestoreActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }

  if (selectedIndex == 5) {
    enterNewActivity(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }

  if (selectedIndex == 6) {
    enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }

  if (selectedIndex == 7) {
    enterNewActivity(new DictionaryPickerActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }

  if (selectedIndex == 8) {
    enterNewActivity(new TarotActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }

  if (selectedIndex == 9) {
#ifndef SIMULATOR
    frontlight.toggle();
#endif
    updateRequired = true;
    return;
  }

  if (onModeSelected) {
    onModeSelected(mode);
  }
}

bool SyncActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) {
    return subActivity->handleTouchTap(x, y);
  }

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  if (x < 0 || x >= screenWidth || y < 0 || y >= screenHeight) {
    return false;
  }

  const int listStartY = mainContentTop() + mainHeaderHeight();
  const int contentBottom = mainContentBottom(renderer);
  const int visibleBottom = INX_THEME.mainTabsAtBottom() ? contentBottom : screenHeight - 80;
  if (y >= listStartY && y < visibleBottom) {
    const int index = (y - listStartY) / LIST_ITEM_HEIGHT;
    if (index >= 0 && index < MENU_ITEM_COUNT) {
      selectedIndex = index;
      updateRequired = true;
      activateSelected();
      return true;
    }
  }

  // The classic bottom hints are visible controls too. Keep the exact same
  // button rectangles as UiRender::buttonHints.
  if (!INX_THEME.mainTabsAtBottom() && y >= screenHeight - 40) {
    const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
    const char* slots[] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
    constexpr int positions[] = {25, 130, 245, 350};
    constexpr int buttonWidth = 106;
    for (int slot = 0; slot < 4; ++slot) {
      if (x < positions[slot] || x >= positions[slot] + buttonWidth || !slots[slot] || slots[slot][0] == '\0') {
        continue;
      }
      const std::string label = slots[slot];
      if (label == "« Recent") {
        if (onRecentOpen) onRecentOpen();
        return true;
      }
      if (label == "Select") {
        activateSelected();
        return true;
      }
    }
  }

  return false;
}

/**
 * Main loop for handling user input and updating the display state.
 * Processes button presses for menu navigation and tab switching.
 */
void SyncActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }

  if (tabSelectorIndex == 3 && updateRequired) {
    updateRequired = false;
    render();
  }

  const bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool upPressed = mappedInput.wasPressed(MenuNav::itemPrev());
  const bool downPressed = mappedInput.wasPressed(MenuNav::itemNext());

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= 300 && onRecentOpen) {
      vTaskDelay(pdMS_TO_TICKS(300));
      onRecentOpen();
    }
    return;
  }

  if (mappedInput.wasPressed(MenuNav::tabPrev())) {
    tabSelectorIndex = 2;
    navigateToSelectedMenu();
    return;
  }

  if (mappedInput.wasPressed(MenuNav::tabNext())) {
    tabSelectorIndex = 4;
    navigateToSelectedMenu();
    return;
  }

  if (tabSelectorIndex != 3) {
    return;
  }

  if (confirmPressed) {
    activateSelected();
    return;
  }

  bool needUpdate = false;

  if (upPressed) {
    selectedIndex = (selectedIndex + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (downPressed) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEM_COUNT;
    needUpdate = true;
  }

  if (needUpdate) {
    updateRequired = true;
  }
}

/**
 * Renders the complete sync activity view including menu items and tab bar.
 */
void SyncActivity::render() const {
  renderer.clearScreen();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int contentBottom = mainContentBottom(renderer);

  renderTabBar(renderer);

  const int headerY = mainContentTop();
  const int headerHeight = mainHeaderHeight();
  const int headerTextY = headerY + (headerHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerTextY, "Device Management", true,
                       EpdFontFamily::BOLD);

  const int dividerY = headerY + headerHeight;
  renderer.line.render(0, dividerY, screenWidth, dividerY);

  const int listStartY = dividerY;
  const int visibleAreaHeight = (INX_THEME.mainTabsAtBottom() ? contentBottom : screenHeight - 80) - listStartY;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int itemY = listStartY + i * LIST_ITEM_HEIGHT;

    if (itemY < listStartY + visibleAreaHeight && itemY + LIST_ITEM_HEIGHT > listStartY) {
      const bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.rectangle.fill(0, itemY, screenWidth, LIST_ITEM_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      const int textX = 20;
      const int titleY = itemY + (LIST_ITEM_HEIGHT - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, titleY, MENU_ITEMS[i], !isSelected);
      if (i == 9) {
#ifndef SIMULATOR
        const char* state = frontlight.isOn() ? "ON" : "OFF";
#else
        const char* state = "OFF";
#endif
        const int stateW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, state);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - stateW - 30, titleY, state, !isSelected,
                             EpdFontFamily::BOLD);
      } else {
        const int caretW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "›");
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - caretW - 30, titleY, "›", !isSelected);
      }

      if (i < MENU_ITEM_COUNT - 1) {
        renderer.line.render(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  }

  const auto labels = mappedInput.mapLabels("« Recent", "Select", "", "");
  renderButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

/**
 * Lifecycle hook called when exiting the activity.
 */
void SyncActivity::onExit() { ActivityWithSubactivity::onExit(); }
