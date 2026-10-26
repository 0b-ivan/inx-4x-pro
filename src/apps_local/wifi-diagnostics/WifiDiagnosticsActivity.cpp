#include "WifiDiagnosticsActivity.h"
#include "../Shelf.h"

#include <algorithm>
#include <cstdio>
#include <WiFi.h>

#ifndef SIMULATOR
#include <Esp.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_wifi.h>
#endif

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "unknown"
#endif

namespace {
#ifndef SIMULATOR
const char* modeName(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_NULL: return "OFF";
    case WIFI_MODE_STA: return "STA";
    case WIFI_MODE_AP: return "AP";
    case WIFI_MODE_APSTA: return "AP+STA";
    default: return "UNKNOWN";
  }
}

const char* powerSaveName(wifi_ps_type_t mode) {
  switch (mode) {
    case WIFI_PS_NONE: return "NONE";
    case WIFI_PS_MIN_MODEM: return "MIN_MODEM";
    case WIFI_PS_MAX_MODEM: return "MAX_MODEM";
    default: return "UNKNOWN";
  }
}

std::string resultText(const char* label, esp_err_t err) {
  char buf[112];
  snprintf(buf, sizeof(buf), "%s: %s (0x%X)",
           label, esp_err_to_name(err), static_cast<unsigned>(err));
  return buf;
}
#endif
}  // namespace

void WifiDiagnosticsActivity::refreshSnapshot() {
  std::vector<std::string> snapshot;
  snapshot.reserve(20);
  snapshot.emplace_back(std::string("Firmware: ") + CROSSPOINT_VERSION);
  char sampleTime[48];
  snprintf(sampleTime, sizeof(sampleTime), "Sample: %lu ms", millis());
  snapshot.emplace_back(sampleTime);

#ifdef SIMULATOR
  // A deterministic, explicitly virtual view. Never probe the computer's Wi-Fi.
  snapshot.emplace_back("Chip: simulated X4 Pro");
  snapshot.emplace_back("ESP-IDF: virtual fixture");
  snapshot.emplace_back("WiFi driver: SIMULATED");
  snapshot.emplace_back("Mode API: simulated");
  snapshot.emplace_back("Radio mode: simulated STA");
  snapshot.emplace_back("STA link: simulated / no RF");
  snapshot.emplace_back("Channel API: simulated");
  snapshot.emplace_back("Channel: fixture only");
  snapshot.emplace_back("Power save: simulated");
  snapshot.emplace_back("STA MAC API: simulated");
#else
  snapshot.emplace_back(std::string("Chip: ") + ESP.getChipModel());
  snapshot.emplace_back(std::string("ESP-IDF: ") + esp_get_idf_version());
  snapshot.emplace_back("WiFi driver: bundled with ESP-IDF");

  // Status queries only: do NOT initialize Wi-Fi, scan, associate, transmit,
  // switch channels, enable monitor mode, or claim ownership of the radio.
  wifi_mode_t radioMode = WIFI_MODE_NULL;
  const esp_err_t modeError = esp_wifi_get_mode(&radioMode);
  snapshot.push_back(resultText("Mode API", modeError));
  if (modeError == ESP_OK) {
    snapshot.emplace_back(std::string("Radio mode: ") + modeName(radioMode));
  } else {
    snapshot.emplace_back("Radio mode: unavailable");
  }

  char line[112];
  const auto status = WiFi.status();
  snprintf(line, sizeof(line), "STA link: %s (%d)",
           status == WL_CONNECTED ? "CONNECTED" : "NOT CONNECTED",
           static_cast<int>(status));
  snapshot.emplace_back(line);
  if (status == WL_CONNECTED) {
    snapshot.emplace_back(std::string("STA IP: ") + WiFi.localIP().toString().c_str());
    snprintf(line, sizeof(line), "STA RSSI: %d dBm", WiFi.RSSI());
    snapshot.emplace_back(line);
  }

  uint8_t channel = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  const esp_err_t channelError = esp_wifi_get_channel(&channel, &secondary);
  snapshot.push_back(resultText("Channel API", channelError));
  if (channelError == ESP_OK) {
    snprintf(line, sizeof(line), "Channel: %u secondary: %d",
             static_cast<unsigned>(channel), static_cast<int>(secondary));
    snapshot.emplace_back(line);
  }

  wifi_ps_type_t powerSave = WIFI_PS_NONE;
  const esp_err_t powerError = esp_wifi_get_ps(&powerSave);
  snapshot.push_back(resultText("Power API", powerError));
  if (powerError == ESP_OK) {
    snapshot.emplace_back(std::string("Power save: ") + powerSaveName(powerSave));
  }

  uint8_t stationMac[6] = {};
  // Validate STA interface availability without displaying its MAC address.
  const esp_err_t macError = esp_wifi_get_mac(WIFI_IF_STA, stationMac);
  snapshot.push_back(resultText("STA MAC API", macError));
#endif

  snapshot.emplace_back("TX: NO FRAMES SENT BY DIAG");
  snapshot.emplace_back("Deauth TX: not documented by IDF");
  snapshot.emplace_back("API success != RF delivery");
  snapshot.emplace_back("Refresh: button or touch");

  // Render runs on its own FreeRTOS task. Publish the complete readout atomically.
  RenderLock lock;
  lines.swap(snapshot);
  scrollOffset = 0;
}

int WifiDiagnosticsActivity::visibleLineCount() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() -
                            BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
  const int lineHeight = std::max(1, renderer.getLineHeight(UI_10_FONT_ID) + 8);
  return std::max(1, (contentBottom - contentTop) / lineHeight);
}

void WifiDiagnosticsActivity::scrollBy(int delta) {
  const int last = std::max(0, static_cast<int>(lines.size()) - visibleLineCount());
  const int next = std::max(0, std::min(last, scrollOffset + delta));
  if (next == scrollOffset) return;
  {
    RenderLock lock;
    scrollOffset = next;
  }
  requestUpdate();
}

void WifiDiagnosticsActivity::onEnter() {
  Activity::onEnter();
  refreshSnapshot();
  requestUpdate();
}

void WifiDiagnosticsActivity::onExit() { Activity::onExit(); }

void WifiDiagnosticsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    refreshSnapshot();
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this] { scrollBy(+1); });
  buttonNavigator.onPrevious([this] { scrollBy(-1); });

  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  switch (GUI.tappedAlwaysHintButton(x, y, renderer)) {
    case 1: shelf::leave(renderer, mappedInput); return;
    case 2: refreshSnapshot(); requestUpdate(); return;
    case 3: scrollBy(-1); return;
    case 4: scrollBy(+1); return;
    default: return;
  }
}

void WifiDiagnosticsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int x = m.contentSidePadding;
  const int step = std::max(1, renderer.getLineHeight(UI_10_FONT_ID) + 8);
  const int visible = visibleLineCount();
  const int last = std::max(0, static_cast<int>(lines.size()) - visible);
  const int first = std::max(0, std::min(last, scrollOffset));

  GUI.drawHeader(renderer, Rect{0, m.topPadding, pageWidth, m.headerHeight}, "WIFI DIAG");
  int y = contentTop;
  for (int i = first; i < static_cast<int>(lines.size()) && i < first + visible; ++i) {
    renderer.drawText(UI_10_FONT_ID, x, y, lines[i].c_str());
    y += step;
  }
  if (last > 0) {
    char pages[24];
    snprintf(pages, sizeof(pages), "%d/%d", first + 1, last + 1);
    renderer.drawText(SMALL_FONT_ID, pageWidth - 70, contentTop, pages);
  }
  const auto labels = mappedInput.mapLabels("Back", "Refresh", "Up", "Down");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
