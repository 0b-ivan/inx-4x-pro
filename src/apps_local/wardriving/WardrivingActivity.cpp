#include "WardrivingActivity.h"
#include "WardriveCsv.h"
#include "WardriveWrite.h"
#include "DevMode.h"
#include "../Shelf.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../radio/RadioManager.h"

void WardrivingActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  RADIO.ensureWifi();
  WiFi.disconnect();
  logging = false;
  scanPending = false;
  storageError = false;
  networks.clear();
  scanCount = 0;
  totalNewThisScan = 0;
  selectorIndex = 0;
  filename.clear();
  requestUpdate();
}

void WardrivingActivity::onExit() {
  stopLogging();
  RADIO.shutdown();
  devmode::resume();
  Activity::onExit();
}

void WardrivingActivity::startLogging() {
  storageError = false;
  Storage.mkdir("/crossplay");
  Storage.mkdir("/crossplay/logs");

  char buf[64];
  snprintf(buf, sizeof(buf), "/crossplay/logs/wardrive_%lu.csv", millis());
  filename = buf;

  if (!Storage.writeFile(filename.c_str(), "SSID,BSSID,RSSI,Channel,Encryption,FirstSeen_ms\n")) {
    storageError = true;
    filename.clear();
    logging = false;
    scanPending = false;
    LOG_ERR("WARD", "Cannot create CSV; no scan data will be recorded");
    requestUpdate();
    return;
  }

  startTime = millis();
  logging = true;
  scanCount = 0;
  totalNewThisScan = 0;

  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
  scanPending = true;
  lastScanTime = millis();

  LOG_DBG("WARD", "Started logging to %s", filename.c_str());
}

void WardrivingActivity::stopLogging() {
  if (!logging) return;
  WiFi.scanDelete();
  scanPending = false;
  logging = false;
  LOG_DBG("WARD", "Stopped logging. %zu unique networks.", networks.size());
}

bool WardrivingActivity::appendNetworkToCsv(const SeenNetwork& net) {
  if (filename.empty()) return false;
  HalFile f = Storage.open(filename.c_str(), O_WRITE | O_CREAT | O_APPEND);
  if (!f) return false;
  const std::string ssid = wardrive_csv::escapeField(net.ssid);
  const String line = String(ssid.c_str()) + "," + net.bssid.c_str() + "," +
                      String(net.rssi) + "," + String(net.channel) + "," +
                      encryptionString(net.encType) + "," + String(net.firstSeen) + "\n";
  return wardrive_write::writeComplete(f, std::string(line.c_str(), line.length()));
}

void WardrivingActivity::processScanResults() {
  if (!scanPending) return;
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;
  scanPending = false;

  int newThisScan = 0;

  if (result > 0) {
    for (int i = 0; i < result; i++) {
      uint8_t* rawBssid = WiFi.BSSID(i);
      if (!rawBssid) continue;
      char bssidBuf[20];
      snprintf(bssidBuf, sizeof(bssidBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
               rawBssid[0], rawBssid[1], rawBssid[2], rawBssid[3], rawBssid[4], rawBssid[5]);

      // Check if BSSID already seen
      bool found = false;
      for (auto& net : networks) {
        if (net.bssid == bssidBuf) {
          net.lastSeen = millis();
          net.rssi = WiFi.RSSI(i);
          found = true;
          break;
        }
      }

      if (!found && static_cast<int>(networks.size()) < MAX_NETWORKS) {
        SeenNetwork net;
        String ssid = WiFi.SSID(i);
        net.ssid = ssid.isEmpty() ? "(hidden)" : ssid.c_str();
        net.bssid = bssidBuf;
        net.rssi = WiFi.RSSI(i);
        net.channel = static_cast<uint8_t>(WiFi.channel(i));
        net.encType = static_cast<uint8_t>(WiFi.encryptionType(i));
        net.firstSeen = millis();
        net.lastSeen = net.firstSeen;

        if (logging && !appendNetworkToCsv(net)) {
          storageError = true;
          LOG_ERR("WARD", "SD write failed, recording stopped");
          stopLogging();
        }
        networks.push_back(std::move(net));
        newThisScan++;
        if (storageError) break;
      }
    }
  }

  WiFi.scanDelete();
  scanCount++;
  totalNewThisScan = newThisScan;
  lastScanTime = millis();
  requestUpdate();
}

const char* WardrivingActivity::encryptionString(uint8_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "?";
  }
}

void WardrivingActivity::loop() {
  if (logging) {
    processScanResults();

    // Advance spinner while waiting for initial scan results
    if (networks.empty()) {
      if (millis() - lastSpinnerUpdate >= 600) {
        lastSpinnerUpdate = millis();
        spinnerFrame = (spinnerFrame + 1) % 3;
        requestUpdate();
      }
    }

    // Start next async scan after interval, but only if the previous one finished
    if (!scanPending && millis() - lastScanTime >= SCAN_INTERVAL_MS) {
      WiFi.scanNetworks(true);
      scanPending = true;
      lastScanTime = millis();
    }
  }

  const int count = static_cast<int>(networks.size());

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
    if (logging) {
      stopLogging();
    } else {
      networks.clear();
      selectorIndex = 0;
      startLogging();
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (logging) stopLogging();
    shelf::leave(renderer, mappedInput);
  }

  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      const int btn = GUI.tappedAlwaysHintButton(tx_, ty_, renderer);
      if (btn == 1) { if (logging) stopLogging(); shelf::leave(renderer, mappedInput); return; }
      if (btn == 2) {
        if (logging) { stopLogging(); } else { networks.clear(); selectorIndex = 0; startLogging(); }
        requestUpdate(); return;
      }
      // On GT911 the two side keys do not provide a Confirm action; the
      // always-visible action bar is the explicit Start/Stop touch control.
      if (count > 0) {
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int contentH = renderer.getScreenHeight() - contentTop - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(true);
        const int pageItems = std::max(1, contentH / rowStep);
        const int pageStart = (selectorIndex / pageItems) * pageItems;
        const int pageCount = std::min(pageItems, count - pageStart);
        const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                              renderer.getScreenWidth(), m.listWithSubtitleRowHeight);
        if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
          selectorIndex = pageStart + touchRow; requestUpdate();
        }
      }
    }
  }
}

void WardrivingActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Build subtitle: "N unique | scan #M"  or  "N unique"
  char subtitleBuf[48];
  if (storageError) {
    snprintf(subtitleBuf, sizeof(subtitleBuf), "SD ERROR | %zu seen", networks.size());
  } else if (logging) {
    snprintf(subtitleBuf, sizeof(subtitleBuf), "%zu unique | scan #%d", networks.size(), scanCount);
  } else {
    snprintf(subtitleBuf, sizeof(subtitleBuf), "%zu unique", networks.size());
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Wardriving", subtitleBuf);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - BaseMetrics::values.buttonHintsHeight - metrics.verticalSpacing;

  if (networks.empty()) {
    if (logging) {
      GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "SCANNING...", spinnerFrame);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                                storageError ? "SD write failed - not recording" : "Press OK to start");
    }
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(networks.size()), selectorIndex,
        [this](int index) -> std::string {
          return networks[index].ssid;
        },
        [this](int index) -> std::string {
          const auto& net = networks[index];
          char buf[40];
          snprintf(buf, sizeof(buf), "%d dBm  Ch%u  %s",
                   net.rssi, net.channel, encryptionString(net.encType));
          return buf;
        });
  }

  const auto labels = mappedInput.mapLabels("« Exit", logging ? "Stop" : "Start",
                                            "^", "v");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
