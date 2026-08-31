/**
 * @file main.cpp
 * @brief Firmware entry point, globals, and activity bootstrap.
 */

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <esp_system.h>
#ifndef SIMULATOR
#include <HalFrontlight.h>
#endif

#include <cstring>
#include <new>
#include <string>

#include "activity/OpdsServerListActivity.h"
#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/LibraryActivity.h"
#include "activity/page/RecentActivity.h"
#include "activity/page/SettingsActivity.h"
#include "activity/page/StatisticActivity.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "state/OpdsServerStore.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#ifndef SIMULATOR
#include "system/QuickSettingsDrawer.h"
#endif
#include "system/UiTheme.h"
#include "util/StringUtils.h"

#ifdef SIMULATOR
extern HalDisplay display;
extern HalGPIO gpio;
#else
HalDisplay display;
HalGPIO gpio;
#endif
MappedInputManager input(gpio);
GfxRenderer renderer(display);
GfxRenderer& render = renderer;
#ifndef SIMULATOR
QuickSettingsDrawer quickSettings(renderer, input);
#endif

Activity* currentActivity = nullptr;
bool sdCardAvailable = false;

unsigned long t1 = 0;
unsigned long t2 = 0;

#ifndef SIMULATOR
namespace {
constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long X4PRO_POWER_CLICK_MAX_HOLD_MS = 300;
unsigned long lastX4ProPowerClickAt = 0;
}  // namespace
#endif

void verifyPowerButtonDuration();
void waitForPowerRelease();
void normalizeUnavailableClockSettings();
void enterDeepSleep();
void onGoToReader(const std::string& path);
void onSelectBook(const std::string& path);
void onGoToRecent();
void onGoToStatistics();
void onGoToFileTransfer();
void onGoToSettings();
void onGoToLibrary(const std::string& path = "/");
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path);
bool handleGlobalPowerRefresh();
bool handleScreenTouch();
#ifndef SIMULATOR
bool handleX4ProFrontlightDoubleClick();
#endif

/**
 * @brief Switches the current activity using standard heap allocation.
 * * This uses 'new' and 'delete' which allows the ReaderActivity to utilize
 * the full 360KB of available heap rather than being stuck in a small static buffer.
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }

  currentActivity = new T(std::forward<Args>(args)...);
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] Activity: %s\n", millis(), currentActivity->getName());
#endif
  currentActivity->onEnter();
}

/**
 * @brief Navigates to the reader activity for a specific book.
 */
void onGoToReader(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [](const std::string&) { onGoToRecent(); });
}

bool isExportedNoteImage(const std::string& path) {
  constexpr const char* root = "/Bookmarks & Annotations";
  const size_t rootLen = strlen(root);
  const bool inRoot = path.compare(0, rootLen, root) == 0 && (path.size() == rootLen || path[rootLen] == '/');
  return inRoot && (StringUtils::checkFileExtension(path, ".bmp") || StringUtils::checkFileExtension(path, ".jpg") ||
                    StringUtils::checkFileExtension(path, ".jpeg") || StringUtils::checkFileExtension(path, ".png"));
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
 */
void openReaderFromCallback(const std::string& path) {
  const std::string pathCopy = path;
  if (isExportedNoteImage(pathCopy)) {
    switchTo<ImageViewerActivity>(render, input, pathCopy, [pathCopy]() {
      std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
      if (folderPath.empty()) folderPath = "/";
      onGoToLibrary(folderPath);
    });
    return;
  }
  switchTo<ReaderActivity>(render, input, pathCopy, [pathCopy](const std::string&) {
    std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
    if (folderPath.empty()) folderPath = "/";
    onGoToLibrary(folderPath);
  });
}

void onSelectBook(const std::string& path) { onGoToReader(path); }

void onGoToStatistics() { switchTo<StatisticActivity>(render, input, onGoToRecent, onGoToFileTransfer); }

void onGoToRecent() {
  switchTo<RecentActivity>(render, input, []() { onGoToLibrary("/"); }, onGoToStatistics, onSelectBook, onGoToRecent);
}

void onNetworkModeSelected(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
      switchTo<LocalNetworkActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CONNECT_CALIBRE:
      switchTo<CalibreConnectActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CREATE_HOTSPOT:
      switchTo<HotspotActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::OPDS_BROWSER:
      switchTo<OpdsServerListActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

void onGoToFileTransfer() {
  switchTo<SyncActivity>(render, input, onNetworkModeSelected, onGoToRecent, onGoToStatistics, onGoToSettings);
}

void onGoToSettings() {
  switchTo<SettingsActivity>(
      render, input, onGoToRecent, []() { onGoToLibrary("/"); }, onGoToFileTransfer, onGoToStatistics);
}

void onGoToLibrary(const std::string& path) {
  switchTo<LibraryActivity>(render, input, onGoToRecent, openReaderFromCallback, onGoToRecent, onGoToSettings, path);
}

void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  bool abort = false;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < SETTINGS.getPowerButtonDuration()) {
      delay(10);
      gpio.update();
    }
    abort = gpio.getHeldTime() < SETTINGS.getPowerButtonDuration();
  } else {
    abort = true;
  }

  if (abort) gpio.startDeepSleep();
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

void normalizeUnavailableClockSettings() {
  if (gpio.hasRtc()) {
    return;
  }

  bool changed = false;
  if (SETTINGS.sleepScreen == SystemSetting::DATETIME) {
    SETTINGS.sleepScreen = SystemSetting::LIGHT;
    changed = true;
  }
  if (SETTINGS.sleepClockRefreshInterval != SystemSetting::CLOCK_REFRESH_OFF) {
    SETTINGS.sleepClockRefreshInterval = SystemSetting::CLOCK_REFRESH_OFF;
    changed = true;
  }
  if (changed) {
    SETTINGS.saveToFile();
  }
}

void enterDeepSleep() {
  normalizeUnavailableClockSettings();
#ifndef SIMULATOR
  frontlight.setOn(false);
#endif
  switchTo<SleepActivity>(render, input);
  display.deepSleep();
  gpio.startDeepSleep();
}

void setupDisplayAndFonts() {
  display.begin();
  render.begin();
  FontManager::initialize(render);
}

#ifndef SIMULATOR
bool handleX4ProFrontlightDoubleClick() {
  if (!gpio.deviceIsX4() || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }

  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  frontlight.toggle();
  return true;
}
#endif

bool handleGlobalPowerRefresh() {
  if (!currentActivity || !currentActivity->allowGlobalPowerRefresh()) {
    return false;
  }
  if (SETTINGS.shortPwrBtn != SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    return false;
  }
  if (!input.wasReleased(MappedInputManager::Button::Power)) {
    return false;
  }

  renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
  return true;
}

/**
 * Dispatch one completed tap in logical screen coordinates.
 *
 * Main tabs are global chrome; everything else belongs to the active activity.
 * Activities hit-test the exact rectangles they render. This mirrors the
 * CrossPoint/FreeInk touch contract and keeps the HAL free of fake buttons.
 */
bool handleScreenTouch() {
  if (!currentActivity) {
    return false;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return false;
  }

  // Modal/activity-owned surfaces (reader selection, thumbnail controls) get
  // first refusal, even when their bottom action row overlaps the global tab bar.
  if (currentActivity->prioritizesScreenTouch() && currentActivity->handleTouchTap(x, y)) {
    Serial.printf("[%lu] [TOUCH] %s priority handled x=%d y=%d\n", millis(), currentActivity->getName(), x, y);
    return true;
  }

  const int tabY = INX_THEME.mainTabBarY(renderer);
  const int tabH = INX_THEME.mainTabBarHeight();
  const int width = renderer.getScreenWidth();
  if (x >= 0 && x < width && y >= tabY && y < tabY + tabH) {
    constexpr int tabCount = 5;
    const int tabButtonWidth = (width / tabCount) - 1;
    int tab = tabButtonWidth > 0 ? x / tabButtonWidth : 0;
    if (tab < 0) tab = 0;
    if (tab >= tabCount) tab = tabCount - 1;

    Serial.printf("[%lu] [TOUCH] main tab=%d x=%d y=%d\n", millis(), tab, x, y);
    switch (tab) {
      case 0:
        onGoToRecent();
        break;
      case 1:
        onGoToLibrary("/");
        break;
      case 2:
        onGoToSettings();
        break;
      case 3:
        onGoToFileTransfer();
        break;
      case 4:
        onGoToStatistics();
        break;
      default:
        return false;
    }
    return true;
  }

  if (currentActivity->handleTouchTap(x, y)) {
    Serial.printf("[%lu] [TOUCH] %s handled x=%d y=%d\n", millis(), currentActivity->getName(), x, y);
    return true;
  }

  // Do not return true here. Legacy components such as HomeMenuDrawer still
  // inspect the same latched FreeInk tap from their normal loop until they are
  // migrated to Activity::handleTouchTap().
  return false;
}

void setup() {
  t1 = millis();
  gpio.begin();
  setupDisplayAndFonts();

  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);
    Serial.printf("[%lu] [BOOT] reset_reason=%d free=%u largest=%u\n", millis(), static_cast<int>(esp_reset_reason()),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }

#ifndef SIMULATOR
  // Initialize after Serial so X4 Pro hardware-validation messages are visible.
  // FreeInk owns PWM polarity, frequency, resolution and warm/cool pin mapping.
  frontlight.begin(60, 50, false);
#endif

  sdCardAvailable = SdMan.begin();

  if (sdCardAvailable) {
    SETTINGS.loadFromFile();
    READER_SETTINGS.loadFromFile();
    OPDS_STORE.loadOrMigrate({"Default", SETTINGS.opdsServerUrl, SETTINGS.opdsUsername, SETTINGS.opdsPassword});
  }
  normalizeUnavailableClockSettings();

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      gpio.startDeepSleep();
      break;
    default:
      break;
  }

  switchTo<BootActivity>(render, input);
  waitForPowerRelease();
}

void loop() {
  gpio.update();
  static unsigned long lastActivityTime = millis();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() ||
      (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    return;
  }

#ifndef SIMULATOR
  // CrossPoint control-center gesture: swipe down from the top edge. While the
  // overlay is open the underlying Activity stays alive but paused, so reader
  // page/list state cannot be destroyed by opening quick settings.
  if (quickSettings.isOpen()) {
    quickSettings.loop();
    delay(10);
    return;
  }

  if (input.wasMenuGesture()) {
    if (quickSettings.open()) lastActivityTime = millis();
    delay(10);
    return;
  }

  if (handleX4ProFrontlightDoubleClick()) {
    delay(10);
    return;
  }
#endif

  if (handleGlobalPowerRefresh()) {
    delay(10);
    return;
  }

  if (handleScreenTouch()) {
    delay(10);
    return;
  }

  if (currentActivity) {
    currentActivity->loop();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();
  } else {
    delay(10);
  }
}
