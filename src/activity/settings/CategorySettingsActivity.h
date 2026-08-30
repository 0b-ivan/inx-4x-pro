#pragma once

/**
 * @file CategorySettingsActivity.h
 * @brief Public interface and types for CategorySettingsActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../Menu.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/SystemSetting.h"
#include "system/UiTheme.h"

class SystemSetting;

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, SEPARATOR, INFO };

enum class GroupType {
  NONE,
  FONT,
  LAYOUT,
  READER_CONTROLS,
  SYSTEM,
  STATUS_BAR,
  DEVICE_DISPLAY,
  CLOCK,
  DEVICE_BUTTONS,
  DEVICE_ADVANCED,
  DEVICE_ACTIONS,
  IMAGE,
};

struct ValueRange {
  uint8_t min;
  uint8_t max;
  uint8_t step;

  ValueRange() : min(0), max(0), step(0) {}
  ValueRange(uint8_t minVal, uint8_t maxVal, uint8_t stepVal) : min(minVal), max(maxVal), step(stepVal) {}
};

struct SettingInfo {
  const char* name;
  SettingType type;
  uint8_t SystemSetting::* valuePtr;
  std::vector<std::string> enumValues;
  std::vector<uint8_t> enumOptionValues;
  ValueRange valueRange;
  GroupType group;

  SettingInfo()
      : name(nullptr), type(SettingType::SEPARATOR), valuePtr(nullptr), valueRange(), group(GroupType::NONE) {}

  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, GroupType g)
      : name(n), type(t), valuePtr(ptr), valueRange(), group(g) {}

  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
              GroupType g)
      : name(n), type(t), valuePtr(ptr), enumValues(values), valueRange(), group(g) {}

  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
              const std::vector<uint8_t>& optionValues, GroupType g)
      : name(n), type(t), valuePtr(ptr), enumValues(values), enumOptionValues(optionValues), valueRange(), group(g) {}

  SettingInfo(const char* n, SettingType t, uint8_t SystemSetting::* ptr, const ValueRange& range, GroupType g)
      : name(n), type(t), valuePtr(ptr), valueRange(range), group(g) {}

  static SettingInfo Toggle(const char* name, uint8_t SystemSetting::* ptr, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::TOGGLE;
    info.valuePtr = ptr;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  static SettingInfo Enum(const char* name, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
                          GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ENUM;
    info.valuePtr = ptr;
    info.enumValues = values;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  static SettingInfo Enum(const char* name, uint8_t SystemSetting::* ptr, const std::vector<std::string>& values,
                          const std::vector<uint8_t>& optionValues, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ENUM;
    info.valuePtr = ptr;
    info.enumValues = values;
    info.enumOptionValues = optionValues;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  static SettingInfo Action(const char* name, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::ACTION;
    info.valuePtr = nullptr;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  static SettingInfo Value(const char* name, uint8_t SystemSetting::* ptr, const ValueRange& valueRange,
                           GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::VALUE;
    info.valuePtr = ptr;
    info.valueRange = valueRange;
    info.group = group;
    return info;
  }

  static SettingInfo Separator(const char* name, GroupType group) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::SEPARATOR;
    info.valueRange = ValueRange();
    info.group = group;
    return info;
  }

  static SettingInfo Info(const char* name, const char* value, GroupType group = GroupType::NONE) {
    SettingInfo info;
    info.name = name;
    info.type = SettingType::INFO;
    info.valuePtr = nullptr;
    info.enumValues = {std::string(value ? value : "")};
    info.group = group;
    return info;
  }
};

extern const int LIST_ITEM_HEIGHT;

class CategorySettingsActivity final : public ActivityWithSubactivity, public Menu {
  static constexpr int kBottomButtonHintsHeight = 50;

  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  bool halfRefreshOnLoadApplied_ = false;
  bool selectorOpen = false;
  uint8_t selectorMode = 0;
  int selectedIndex = 0;
  int scrollOffset = 0;
  int itemsPerPage = 0;
  int selectorSourceIndex = -1;
  int selectorSelectedIndex = 0;
  int selectorScrollOffset = 0;
  const char* categoryName;
  std::vector<SettingInfo> settingsStorage;
  const SettingInfo* settingsList;
  int settingsCount;
  const std::function<void()> onGoBack;
  const std::function<void()> onIndexLibrary;
  const std::function<void()> onAboutPanel;
  const char* backButtonLabel;
  const std::function<void()> onTabRecent;
  const std::function<void()> onTabLibrary;
  const std::function<void()> onTabSync;
  const std::function<void()> onTabStatistics;

  struct MenuEntry {
    const char* name;
    SettingType type;
    uint8_t SystemSetting::* valuePtr;
    ValueRange valueRange;
    GroupType group;
    const SettingInfo* setting;
    std::function<const char*()> getValueText;
    std::function<void(int)> change;
  };

  static constexpr size_t kGroupCount = static_cast<size_t>(GroupType::IMAGE) + 1;
  static constexpr size_t groupIndex(const GroupType group) { return static_cast<size_t>(group); }
  bool isGroupExpanded(GroupType group) const { return groupExpanded_[groupIndex(group)]; }

  std::vector<MenuEntry> menuItems;
  std::vector<std::string> selectorOptions;
  std::vector<std::string> selectorValues;
  std::array<bool, kGroupCount> groupExpanded_{};

  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render();
  void setupMenu();
  void applyChange(int delta);
  void openSelectorForSelected();
  void openSleepImageSelector();
  bool rebuildSleepImageIndex();
  void loadSleepImageIndexRows();
  void applySleepImageSelection();
  void moveSelector(int delta);
  void selectorPage(int delta);
  void closeSelector(bool save);
  void renderSelectorOverlay();
  int selectedOptionIndex(const MenuEntry& entry) const;
  void applySelectedOption(MenuEntry& entry, int optionIndex);
  void toggleGroup(GroupType group);

  void navigateToSelectedMenu() override;

 public:
  CategorySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* categoryName,
                           std::vector<SettingInfo> settings, const std::function<void()>& onGoBack,
                           std::function<void()> indexLibraryHandler = nullptr,
                           std::function<void()> aboutPanelHandler = nullptr, const char* backLabel = nullptr,
                           std::function<void()> tabNavigateRecent = nullptr,
                           std::function<void()> tabNavigateLibrary = nullptr,
                           std::function<void()> tabNavigateSync = nullptr,
                           std::function<void()> tabNavigateStatistics = nullptr)
      : ActivityWithSubactivity("CategorySettings", renderer, mappedInput),
        Menu(),
        categoryName(categoryName),
        settingsStorage(std::move(settings)),
        settingsList(settingsStorage.data()),
        settingsCount(static_cast<int>(settingsStorage.size())),
        onGoBack(onGoBack),
        onIndexLibrary(std::move(indexLibraryHandler)),
        onAboutPanel(std::move(aboutPanelHandler)),
        backButtonLabel(backLabel),
        onTabRecent(std::move(tabNavigateRecent)),
        onTabLibrary(std::move(tabNavigateLibrary)),
        onTabSync(std::move(tabNavigateSync)),
        onTabStatistics(std::move(tabNavigateStatistics)) {
    tabSelectorIndex = 2;
    const int contentTop = mainContentTop() + mainHeaderHeight();
    const int contentBottom = INX_THEME.mainTabsAtBottom()
                                  ? mainContentBottom(renderer) - kBottomButtonHintsHeight
                                  : renderer.getScreenHeight() - 80;
    itemsPerPage = (contentBottom - contentTop) / UiTheme::DRAWER_LIST_ITEM_HEIGHT;
    if (itemsPerPage < 1) itemsPerPage = 1;

    groupExpanded_.fill(false);
  }

  bool handleTouchTap(const int x, const int y) override {
    if (subActivity) {
      return subActivity->handleTouchTap(x, y);
    }
    if (x < 0 || x >= renderer.getScreenWidth() || y < 0 || y >= renderer.getScreenHeight()) {
      return false;
    }

    // Selector overlay: tapping a visible option selects and confirms it.
    if (selectorOpen && !selectorOptions.empty()) {
      constexpr int rowHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT - 4;
      const int headerHeight = INX_THEME.drawerHeaderHeight() - 4;
      constexpr int visibleRows = 5;
      const int rows = std::min(visibleRows, static_cast<int>(selectorOptions.size()));
      const int panelW = std::min(renderer.getScreenWidth() - 24, 360);
      const int panelH = headerHeight + rows * rowHeight;
      const int panelX = (renderer.getScreenWidth() - panelW) / 2;
      const int panelY = std::max(mainContentTop() + 8, (renderer.getScreenHeight() - panelH) / 2);
      const int rowsY = panelY + headerHeight;
      if (x >= panelX && x < panelX + panelW && y >= rowsY && y < rowsY + rows * rowHeight) {
        const int visibleRow = (y - rowsY) / rowHeight;
        const int optionIndex = selectorScrollOffset + visibleRow;
        if (optionIndex >= 0 && optionIndex < static_cast<int>(selectorOptions.size())) {
          selectorSelectedIndex = optionIndex;
          closeSelector(true);
          return true;
        }
      }
      return false;
    }

    // Settings list: use exactly the row placement from render(), including
    // invisible blank separators. A tap directly invokes the row's normal
    // Confirm action instead of synthesizing a hardware button.
    const int startY = mainContentTop() + mainHeaderHeight();
    constexpr int itemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
    int visibleCount = 0;
    for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); ++i) {
      const int index = i + scrollOffset;
      const auto& entry = menuItems[index];
      if (entry.type == SettingType::SEPARATOR && (entry.name == nullptr || entry.name[0] == '\0')) {
        continue;
      }
      const int itemY = startY + visibleCount * itemHeight;
      ++visibleCount;
      if (y < itemY || y >= itemY + itemHeight) {
        continue;
      }

      selectedIndex = index;
      updateRequired = true;
      if (entry.type == SettingType::SEPARATOR) {
        toggleGroup(entry.group);
      } else if (entry.type == SettingType::ACTION) {
        entry.change(0);
      } else if (entry.type == SettingType::INFO) {
        // Read-only row: selection highlight only.
      } else if (entry.type == SettingType::ENUM || entry.type == SettingType::VALUE) {
        openSelectorForSelected();
      } else {
        applyChange(1);
      }
      return true;
    }
    return false;
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
};
