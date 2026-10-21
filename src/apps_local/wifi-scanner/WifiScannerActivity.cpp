#include "WifiScannerActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <climits>

#include "MappedInputManager.h"
#include "apps_local/Shelf.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---- Factory ----------------------------------------------------------------

std::unique_ptr<Activity> WifiScannerActivity::create(GfxRenderer& renderer,
                                                      MappedInputManager& mappedInput) {
  return std::make_unique<WifiScannerActivity>(renderer, mappedInput);
}

// ---- Static helpers ---------------------------------------------------------

float WifiScannerActivity::rssiToFraction(int32_t rssi) {
  static constexpr int32_t kMin = -100;
  static constexpr int32_t kMax = -30;
  if (rssi <= kMin) return 0.0f;
  if (rssi >= kMax) return 1.0f;
  return static_cast<float>(rssi - kMin) / static_cast<float>(kMax - kMin);
}

const char* WifiScannerActivity::qualityLabel(int32_t rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "Fair";
  return "Weak";
}

const char* WifiScannerActivity::encryptionString(uint8_t type) {
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

const char* WifiScannerActivity::signalBars(int32_t rssi) {
  if (rssi >= -50) return "||||";
  if (rssi >= -60) return "|||.";
  if (rssi >= -70) return "||..";
  if (rssi >= -80) return "|...";
  return "....";
}

// ---- Lifecycle --------------------------------------------------------------

void WifiScannerActivity::onEnter() {
  Activity::onEnter();
  state = State::Scanning;
  channelViewMode = ChannelViewMode::Spectrum;
  networks.clear();
  selectorIndex = 0;
  sortBySignal = true;
  targetIndex = -1;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  startScan();
  requestUpdate();
}

void WifiScannerActivity::onExit() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_MODE_NULL);
  Activity::onExit();
}

// ---- Scan -------------------------------------------------------------------

void WifiScannerActivity::startScan() {
  state = State::Scanning;
  networks.clear();
  selectorIndex = 0;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
}

void WifiScannerActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  if (result > 0) {
    networks.reserve(static_cast<size_t>(result));
    for (int i = 0; i < result; i++) {
      Network net;
      net.ssid = WiFi.SSID(i).c_str();
      if (net.ssid.empty()) net.ssid = "(hidden)";
      net.rssi = WiFi.RSSI(i);
      net.channel = WiFi.channel(i);
      net.encType = WiFi.encryptionType(i);

      const uint8_t* bssid = WiFi.BSSID(i);
      char buf[20];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
      net.bssid = buf;

      networks.push_back(std::move(net));
    }
    sortNetworks();
    analyzeChannels();
  }
  WiFi.scanDelete();
  state = State::List;
  requestUpdate();
}

void WifiScannerActivity::sortNetworks() {
  if (sortBySignal) {
    std::sort(networks.begin(), networks.end(),
              [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  } else {
    std::sort(networks.begin(), networks.end(),
              [](const Network& a, const Network& b) { return a.ssid < b.ssid; });
  }
}

void WifiScannerActivity::saveToCsv() {
  Storage.mkdir("/crossplay");
  Storage.mkdir("/crossplay/wifi-scanner");
  char filename[64];
  snprintf(filename, sizeof(filename), "/crossplay/wifi-scanner/scan_%lu.csv", millis());

  String csv = "SSID,BSSID,RSSI,Channel,Encryption\n";
  for (const auto& net : networks) {
    csv += String(net.ssid.c_str()) + "," + net.bssid.c_str() + "," +
           String(net.rssi) + "," + String(net.channel) + "," +
           encryptionString(net.encType) + "\n";
  }
  Storage.writeFile(filename, csv);
  LOG_DBG("WSCAN", "Saved %zu networks to %s", networks.size(), filename);
}

// ---- Signal meter -----------------------------------------------------------

void WifiScannerActivity::doMeasurement() {
  if (targetIndex < 0 || targetIndex >= static_cast<int>(networks.size())) return;

  const uint8_t ch = networks[targetIndex].channel;
  const std::string& targetBssid = networks[targetIndex].bssid;

  int n = WiFi.scanNetworks(false, true, false, 300, ch);

  int32_t found = INT32_MIN;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      const uint8_t* bssidBytes = WiFi.BSSID(i);
      if (!bssidBytes) continue;
      char bssidStr[20];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               bssidBytes[0], bssidBytes[1], bssidBytes[2],
               bssidBytes[3], bssidBytes[4], bssidBytes[5]);
      if (targetBssid == bssidStr) {
        found = WiFi.RSSI(i);
        break;
      }
    }
  }
  WiFi.scanDelete();

  if (found == INT32_MIN) {
    requestUpdate();
    return;
  }

  currentRssi = found;
  sampleCount++;
  rssiSum += found;
  avgRssi = static_cast<int32_t>(rssiSum / sampleCount);

  if (sampleCount == 1) {
    minRssi = found;
    maxRssi = found;
  } else {
    if (found < minRssi) minRssi = found;
    if (found > maxRssi) maxRssi = found;
  }

  rssiHistory[rssiHistoryIndex] = found;
  rssiHistoryIndex = (rssiHistoryIndex + 1) % kRssiHistorySize;
  if (rssiHistoryCount < kRssiHistorySize) rssiHistoryCount++;

  requestUpdate();
}

// ---- Channel analysis -------------------------------------------------------

void WifiScannerActivity::analyzeChannels() {
  for (int ch = 1; ch <= 13; ch++) {
    channelData[ch] = {};
    channelData[ch].strongestRssi = -100;
    channelData[ch].avgRssi = -100;
  }

  for (const auto& net : networks) {
    int ch = static_cast<int>(net.channel);
    if (ch < 1 || ch > 13) continue;
    channelData[ch].apCount++;
    if (net.rssi > channelData[ch].strongestRssi) channelData[ch].strongestRssi = net.rssi;
    channelData[ch].rssiSum += net.rssi;
  }

  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].apCount > 0) {
      channelData[ch].avgRssi =
          static_cast<int32_t>(channelData[ch].rssiSum / channelData[ch].apCount);
    }
  }

  for (const auto& net : networks) {
    int apCh = static_cast<int>(net.channel);
    if (apCh < 1 || apCh > 13) continue;
    int weight = 100 + net.rssi;
    if (weight < 1) weight = 1;
    for (int t = 1; t <= 13; t++) {
      int dist = apCh - t;
      if (dist < 0) dist = -dist;
      if (dist > 2) continue;
      int overlap = (dist == 0) ? 100 : (dist == 1 ? 70 : 30);
      channelData[t].interferenceScore += weight * overlap / 100;
    }
  }

  recommendedChannel = findBestChannel();
}

int WifiScannerActivity::findBestChannel() const {
  static constexpr int kPreferred[] = {1, 6, 11};
  int bestScore = INT_MAX;
  int bestCh = 1;

  for (int p : kPreferred) {
    if (channelData[p].interferenceScore < bestScore) {
      bestScore = channelData[p].interferenceScore;
      bestCh = p;
    }
  }
  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].interferenceScore < bestScore) {
      bestScore = channelData[ch].interferenceScore;
      bestCh = ch;
    }
  }
  return bestCh;
}

// ---- Loop -------------------------------------------------------------------

void WifiScannerActivity::loop() {
  mappedInput.update();

  if (state == State::Scanning) {
    processScanResults();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
    }
    return;
  }

  if (state == State::Detail) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::List;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Enter signal meter for selected AP
      targetIndex = selectorIndex;
      sampleCount = 0;
      rssiSum = 0;
      rssiHistoryIndex = 0;
      rssiHistoryCount = 0;
      lastMeasureTime = 0;
      state = State::SignalMeter;
      requestUpdate();
    }
    return;
  }

  if (state == State::SignalMeter) {
    unsigned long now = millis();
    if (now - lastMeasureTime >= kMeasureIntervalMs) {
      lastMeasureTime = now;
      doMeasurement();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::Detail;
      requestUpdate();
    }
    return;
  }

  if (state == State::ChannelView) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::List;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Toggle spectrum/table
      channelViewMode = (channelViewMode == ChannelViewMode::Spectrum)
                            ? ChannelViewMode::Table
                            : ChannelViewMode::Spectrum;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      startScan();
      state = State::Scanning;
      requestUpdate();
    }
    return;
  }

  // ---- List state ---------------------------------------------------------
  buttonNavigator.update(mappedInput, static_cast<int>(networks.size()));

  if (buttonNavigator.moved()) {
    selectorIndex = buttonNavigator.index();
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!networks.empty()) {
      state = State::Detail;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    // Sort toggle
    sortBySignal = !sortBySignal;
    sortNetworks();
    selectorIndex = 0;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    // Channel view
    state = State::ChannelView;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    // Rescan
    startScan();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

// ---- Render -----------------------------------------------------------------

void WifiScannerActivity::render(RenderLock&&) {
  auto& r = renderer;
  r.clearFrameBuffer();

  switch (state) {
    case State::Scanning: {
      GUI.drawMainMenuHeader(r, "WIFI SCANNER");
      int cx = r.getScreenWidth() / 2;
      int cy = r.getScreenHeight() / 2;
      r.drawText(FONT_UI_MEDIUM, cx, cy - 12, "Scanning...", true);
      break;
    }
    case State::List:
      renderList(r);
      break;
    case State::Detail:
      renderDetail(r);
      break;
    case State::SignalMeter:
      renderSignalMeter(r);
      break;
    case State::ChannelView:
      if (channelViewMode == ChannelViewMode::Spectrum) {
        renderChannelSpectrum(r);
      } else {
        renderChannelTable(r);
      }
      break;
  }

  r.flushFrameBuffer();
}

void WifiScannerActivity::renderList(GfxRenderer& r) {
  GUI.drawMainMenuHeader(r, "WIFI SCANNER");

  int w = r.getScreenWidth();
  int h = r.getScreenHeight();
  int headerH = GUI.headerHeight();
  int footerH = GUI.footerHeight();
  int rowH = GUI.listRowHeight();
  int y = headerH;
  int visibleRows = (h - headerH - footerH) / rowH;

  if (networks.empty()) {
    r.drawText(FONT_UI_MEDIUM, w / 2, h / 2, "No networks found", true);
    GUI.drawMainMenuFooter(r, "RESCAN", sortBySignal ? "A-Z" : "RSSI", "CH");
    return;
  }

  int scroll = std::max(0, selectorIndex - visibleRows + 1);

  for (int i = 0; i < visibleRows; i++) {
    int idx = scroll + i;
    if (idx >= static_cast<int>(networks.size())) break;

    bool selected = (idx == selectorIndex);
    int rowY = y + i * rowH;

    if (selected) {
      r.fillRect(0, rowY, w, rowH, false);
    }

    const auto& net = networks[idx];

    // SSID — left column
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "%s", net.ssid.c_str());
    r.drawText(FONT_UI_SMALL, 8, rowY + 2, ssid, selected);

    // Signal bars + dBm — right column
    char info[24];
    snprintf(info, sizeof(info), "%s %d", signalBars(net.rssi), net.rssi);
    r.drawText(FONT_UI_SMALL, w - 90, rowY + 2, info, selected);
  }

  char footer[32];
  snprintf(footer, sizeof(footer), "%zu AP  CH%d best", networks.size(), recommendedChannel);
  GUI.drawMainMenuFooter(r, footer, sortBySignal ? "A-Z" : "RSSI", "CH");
}

void WifiScannerActivity::renderDetail(GfxRenderer& r) {
  if (selectorIndex >= static_cast<int>(networks.size())) return;
  const auto& net = networks[selectorIndex];

  GUI.drawMainMenuHeader(r, "AP DETAIL");

  int x = 16;
  int y = GUI.headerHeight() + 8;
  int lineH = 22;

  char buf[64];

  snprintf(buf, sizeof(buf), "SSID:  %s", net.ssid.c_str());
  r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
  y += lineH;

  snprintf(buf, sizeof(buf), "BSSID: %s", net.bssid.c_str());
  r.drawText(FONT_UI_SMALL, x, y, buf, false);
  y += lineH;

  snprintf(buf, sizeof(buf), "RSSI:  %d dBm  (%s)", net.rssi, qualityLabel(net.rssi));
  r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
  y += lineH;

  snprintf(buf, sizeof(buf), "CH:    %u", net.channel);
  r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
  y += lineH;

  snprintf(buf, sizeof(buf), "ENC:   %s", encryptionString(net.encType));
  r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
  y += lineH + 4;

  // Signal bar
  int barW = r.getScreenWidth() - 32;
  int barH = 14;
  float frac = rssiToFraction(net.rssi);
  r.drawRect(x, y, barW, barH, false);
  r.fillRect(x, y, static_cast<int>(frac * barW), barH, false);

  GUI.drawMainMenuFooter(r, "BACK", "SIGNAL METER", "");
}

void WifiScannerActivity::renderSignalMeter(GfxRenderer& r) {
  if (targetIndex < 0 || targetIndex >= static_cast<int>(networks.size())) return;
  const auto& net = networks[targetIndex];

  GUI.drawMainMenuHeader(r, "SIGNAL METER");

  int w = r.getScreenWidth();
  int x = 16;
  int y = GUI.headerHeight() + 8;
  int lineH = 22;

  char buf[48];
  snprintf(buf, sizeof(buf), "%s", net.ssid.c_str());
  r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
  y += lineH;

  if (sampleCount > 0) {
    snprintf(buf, sizeof(buf), "Now: %d dBm  %s", currentRssi, qualityLabel(currentRssi));
    r.drawText(FONT_UI_MEDIUM, x, y, buf, false);
    y += lineH;

    snprintf(buf, sizeof(buf), "Min/Avg/Max: %d / %d / %d", minRssi, avgRssi, maxRssi);
    r.drawText(FONT_UI_SMALL, x, y, buf, false);
    y += lineH + 4;

    // History graph
    int graphW = w - 32;
    int graphH = 60;
    r.drawRect(x, y, graphW, graphH, false);

    if (rssiHistoryCount > 1) {
      int colW = graphW / kRssiHistorySize;
      if (colW < 1) colW = 1;
      for (int i = 0; i < rssiHistoryCount; i++) {
        int hi = (rssiHistoryIndex - rssiHistoryCount + i + kRssiHistorySize) % kRssiHistorySize;
        float frac = rssiToFraction(rssiHistory[hi]);
        int barH = static_cast<int>(frac * (graphH - 2));
        int bx = x + 1 + i * colW;
        int by = y + graphH - 1 - barH;
        r.fillRect(bx, by, colW, barH, false);
      }
    }
  } else {
    r.drawText(FONT_UI_MEDIUM, x, y, "Measuring...", false);
  }

  GUI.drawMainMenuFooter(r, "BACK", "", "");
}

void WifiScannerActivity::renderChannelSpectrum(GfxRenderer& r) {
  GUI.drawMainMenuHeader(r, "CHANNEL VIEW");

  int w = r.getScreenWidth();
  int h = r.getScreenHeight();
  int headerH = GUI.headerHeight();
  int footerH = GUI.footerHeight();
  int graphH = h - headerH - footerH - 28;
  int graphY = headerH + 4;
  int colW = (w - 16) / 13;

  int maxAp = 1;
  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].apCount > maxAp) maxAp = channelData[ch].apCount;
  }

  for (int ch = 1; ch <= 13; ch++) {
    int bx = 8 + (ch - 1) * colW;
    int barH = (channelData[ch].apCount * (graphH - 2)) / maxAp;
    int by = graphY + graphH - barH;

    if (ch == recommendedChannel) {
      r.fillRect(bx, graphY, colW - 2, graphH, false);
    } else {
      r.drawRect(bx, graphY, colW - 2, graphH, false);
      if (barH > 0) r.fillRect(bx + 1, by, colW - 3, barH, false);
    }

    // Channel number label
    char lbl[4];
    snprintf(lbl, sizeof(lbl), "%d", ch);
    r.drawText(FONT_UI_SMALL, bx + 2, graphY + graphH + 2, lbl, ch == recommendedChannel);
  }

  char footer[48];
  snprintf(footer, sizeof(footer), "Best: CH%d", recommendedChannel);
  GUI.drawMainMenuFooter(r, "BACK", footer, "TABLE");
}

void WifiScannerActivity::renderChannelTable(GfxRenderer& r) {
  GUI.drawMainMenuHeader(r, "CHANNEL TABLE");

  int x = 8;
  int y = GUI.headerHeight() + 4;
  int lineH = 18;
  int w = r.getScreenWidth();

  r.drawText(FONT_UI_SMALL, x, y, "CH  APs  Strongest  Interference", false);
  y += lineH;
  r.drawLine(x, y, w - x, y, false);
  y += 4;

  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].apCount == 0) continue;
    bool best = (ch == recommendedChannel);
    char row[48];
    snprintf(row, sizeof(row), "%2d  %3d  %4d dBm  %d",
             ch, channelData[ch].apCount, channelData[ch].strongestRssi,
             channelData[ch].interferenceScore);
    r.drawText(FONT_UI_SMALL, x, y, row, best);
    y += lineH;
  }

  GUI.drawMainMenuFooter(r, "BACK", "", "SPECTRUM");
}
