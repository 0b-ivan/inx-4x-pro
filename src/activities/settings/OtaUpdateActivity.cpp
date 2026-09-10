#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#ifndef SIMULATOR
#include <Preferences.h>
#endif

#include <algorithm>
#include <string>

#include "DevMode.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/OtaUpdater.h"

#ifndef SIMULATOR
#include "network/FirmwareBoardTag.h"
#endif

namespace {
constexpr char releasesUrl[] = "https://api.github.com/repos/0b-ivan/inx-4x-pro/releases?per_page=30";
constexpr char otaPrefsNamespace[] = "ota-update";
constexpr char otaChannelKey[] = "channel";
constexpr uint8_t maxPersistedChannel = 1;

uint8_t loadPersistedOtaChannel() {
#ifdef SIMULATOR
  return 0;
#else
  Preferences prefs;
  if (!prefs.begin(otaPrefsNamespace, true)) return 0;
  const uint8_t channel = prefs.getUChar(otaChannelKey, 0);
  prefs.end();
  return channel <= maxPersistedChannel ? channel : 0;
#endif
}

void savePersistedOtaChannel(const uint8_t channel) {
#ifndef SIMULATOR
  Preferences prefs;
  if (!prefs.begin(otaPrefsNamespace, false)) return;
  prefs.putUChar(otaChannelKey, channel <= maxPersistedChannel ? channel : 0);
  prefs.end();
#else
  (void)channel;
#endif
}

const char* releaseAssetName() {
#ifdef SIMULATOR
  // The X4 Pro release contract is the legacy firmware.bin asset name.  The
  // native simulator has no board tag header, so keep the same value here.
  return "firmware.bin";
#else
  return CROSSPOINT_RELEASE_ASSET;
#endif
}
}  // namespace

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, loading release catalog");
  loadReleaseCatalog();
}

void OtaUpdateActivity::loadReleaseCatalog() {
  {
    RenderLock lock(*this);
    state = CHECKING_RELEASES;
    failedDetail = nullptr;
  }
  requestUpdateAndWait();

  const bool includePrerelease = releaseChannel == CHANNEL_PRERELEASE;
  releaseCatalog.setFirmwareAssetName(releaseAssetName());
  releaseCatalog.setIncludePrerelease(includePrerelease);
  releaseCatalog.reset();

  const bool ok = HttpDownloader::fetchUrl(releasesUrl, [this](const uint8_t* data, const size_t len) {
    releaseCatalog.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release catalog fetch failed (HTTP %d)", HttpDownloader::lastStatus());
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  releaseCatalog.prepare(includePrerelease);
  LOG_INF("OTA", "Release catalog: %u compatible entries (%s channel)", static_cast<unsigned>(releaseCatalog.count()),
          includePrerelease ? "pre-release" : "stable");

  if (releaseCatalog.count() == 0) {
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    selectedRow = releaseChannel == CHANNEL_PRERELEASE ? 1 : 0;
    releaseTop = 0;
    state = BROWSING_RELEASES;
  }
  requestUpdate();
}

void OtaUpdateActivity::setReleaseChannel(const uint8_t channel) {
  const uint8_t normalized = channel == CHANNEL_PRERELEASE ? CHANNEL_PRERELEASE : CHANNEL_STABLE;
  if (releaseChannel == normalized) return;

  releaseChannel = normalized;
  savePersistedOtaChannel(releaseChannel);
  loadReleaseCatalog();
}

void OtaUpdateActivity::activateSelectedRow() {
  if (selectedRow == 0) {
    setReleaseChannel(CHANNEL_STABLE);
    return;
  }
  if (selectedRow == 1) {
    setReleaseChannel(CHANNEL_PRERELEASE);
    return;
  }

  const size_t releaseIndex = static_cast<size_t>(selectedRow - 2);
  const auto* entry = releaseCatalog.at(releaseIndex);
  if (entry == nullptr || !updater.selectRelease(entry->tag, entry->firmwareUrl, entry->firmwareSize)) {
    LOG_ERR("OTA", "Failed to select release row %d", selectedRow);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  confirmPopup.show("Install firmware", options, 2, 1, [this](const int idx) {
    if (idx == 1) {
      runUpdateInstall();
    } else {
      {
        RenderLock lock(*this);
        state = BROWSING_RELEASES;
      }
      requestUpdate();
    }
  });
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  releaseChannel = loadPersistedOtaChannel();
  selectedRow = releaseChannel == CHANNEL_PRERELEASE ? 1 : 0;
  releaseTop = 0;

  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::keepSelectionVisible(const size_t visibleReleaseRows) {
  if (selectedRow < 2 || visibleReleaseRows == 0) return;

  const size_t selectedRelease = static_cast<size_t>(selectedRow - 2);
  if (selectedRelease < releaseTop) {
    releaseTop = selectedRelease;
  } else if (selectedRelease >= releaseTop + visibleReleaseRows) {
    releaseTop = selectedRelease - visibleReleaseRows + 1;
  }
}

void OtaUpdateActivity::renderReleaseBrowser() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int height = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowStep = height + metrics.verticalSpacing;

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, "Channel");
  y += rowStep;

  const std::string stable = std::string(selectedRow == 0 ? "> " : "  ") + "Stable";
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, stable.c_str());
  y += rowStep;

  const std::string prerelease = std::string(selectedRow == 1 ? "> " : "  ") + "Nightly / Pre-release";
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, prerelease.c_str());
  y += rowStep + metrics.verticalSpacing;

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, "Available versions");
  y += rowStep;

  const int usableBottom = pageHeight - height * 2;
  const size_t visibleReleaseRows = static_cast<size_t>(std::max(1, (usableBottom - y) / rowStep));
  keepSelectionVisible(visibleReleaseRows);

  const size_t end = std::min(releaseCatalog.count(), releaseTop + visibleReleaseRows);
  for (size_t i = releaseTop; i < end; ++i) {
    const auto* entry = releaseCatalog.at(i);
    if (entry == nullptr) continue;
    const std::string line = std::string(selectedRow == static_cast<int>(i + 2) ? "> " : "  ") + entry->tag;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line.c_str());
    y += rowStep;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "Select", "Previous", "Next");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 state == BROWSING_RELEASES ? "OTA Updates" : tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_RELEASES) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == BROWSING_RELEASES) {
    renderReleaseBrowser();
  } else if (state == WAITING_CONFIRMATION) {
    const int infoTop = pageHeight / 6;
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop,
                      (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, infoTop + height + metrics.verticalSpacing,
                      (std::string("Selected version: ") + updater.getLatestVersion()).c_str());

    if (confirmPopup.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    y += height + metrics.verticalSpacing;
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, "No compatible releases", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, tr(STR_POWER_ON_HINT));
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_INF("OTA", "Installing explicitly selected release %s", updater.getLatestVersion().c_str());
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this,
      true);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE)
                     : res == OtaUpdater::TOO_LARGE_ERROR  ? tr(STR_FIRMWARE_TOO_LARGE)
                                                           : nullptr;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (state == BROWSING_RELEASES) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int height = renderer.getLineHeight(UI_10_FONT_ID);
    const int rowStep = height + metrics.verticalSpacing;
    const int channelRowsTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + rowStep;
    const int versionRowsTop = channelRowsTop + rowStep * 2 + metrics.verticalSpacing + rowStep;
    const int usableBottom = pageHeight - height * 2;
    const size_t visibleReleaseRows = static_cast<size_t>(std::max(1, (usableBottom - versionRowsTop) / rowStep));
    const int totalRows = static_cast<int>(releaseCatalog.count()) + 2;

    const bool previous = mappedInput.wasPressed(MappedInputManager::Button::NavPrevious) ||
                          mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                          mappedInput.wasPressed(MappedInputManager::Button::PageBack);
    const bool next = mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
                      mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                      mappedInput.wasPressed(MappedInputManager::Button::PageForward);

    if (previous && selectedRow > 0) {
      --selectedRow;
      keepSelectionVisible(visibleReleaseRows);
      requestUpdate();
      return;
    }
    if (next && selectedRow + 1 < totalRows) {
      ++selectedRow;
      keepSelectionVisible(visibleReleaseRows);
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      activateSelectedRow();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    int touchedRow = -1;
    const auto channelTouch = mappedInput.rowTouch(touchedRow, channelRowsTop, rowStep, 2, metrics.contentSidePadding,
                                                   pageWidth - metrics.contentSidePadding, height);
    if (channelTouch != MappedInputManager::RowTouch::None) {
      selectedRow = touchedRow;
      requestUpdate();
      if (channelTouch == MappedInputManager::RowTouch::Tap) activateSelectedRow();
      return;
    }

    const int visibleCount = static_cast<int>(std::min(visibleReleaseRows, releaseCatalog.count() - releaseTop));
    touchedRow = -1;
    const auto releaseTouch = mappedInput.rowTouch(touchedRow, versionRowsTop, rowStep, visibleCount,
                                                   metrics.contentSidePadding, pageWidth - metrics.contentSidePadding,
                                                   height);
    if (releaseTouch != MappedInputManager::RowTouch::None) {
      selectedRow = static_cast<int>(releaseTop) + touchedRow + 2;
      requestUpdate();
      if (releaseTouch == MappedInputManager::RowTouch::Tap) activateSelectedRow();
      return;
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up && selectedRow + 1 < totalRows) {
      ++selectedRow;
      keepSelectionVisible(visibleReleaseRows);
      requestUpdate();
    } else if (swipe == MappedInputManager::SwipeDir::Down && selectedRow > 0) {
      --selectedRow;
      keepSelectionVisible(visibleReleaseRows);
      requestUpdate();
    }
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    {
      RenderLock lock(*this);
      state = BROWSING_RELEASES;
    }
    requestUpdate();
    return;
  }

  if (state == FAILED || state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
