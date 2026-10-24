#include "WifiScannerActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <climits>
#include <string>

#include "DevMode.h"
#include "FreeInkUICore.h"
#include "MappedInputManager.h"
#include "apps_local/Shelf.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ---- Factory ----------------------------------------------------------------

std::unique_ptr<Activity> WifiScannerActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
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

const char* WifiScannerActivity::encryptionString(uint8_t type) const {
  switch (type) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/2";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/3";
    default:
      return "?";
  }
}

const char* WifiScannerActivity::signalBars(int32_t rssi) const {
  if (rssi >= -50) return "||||";
  if (rssi >= -60) return "|||.";
  if (rssi >= -70) return "||..";
  if (rssi >= -80) return "|...";
  return "....";
}

// ---- Lifecycle --------------------------------------------------------------

void WifiScannerActivity::onEnter() {
  Activity::onEnter();
  state = SCANNING;
  viewMode = LIST_VIEW;
  channelViewMode = VIEW_SPECTRUM;
  networks.clear();
  selectorIndex = 0;
  sortBySignal = true;
  targetIndex = -1;
  // Scanning walks off the AP channel for seconds at a time, so yield Developer
  // Mode's radio for the lifetime of the scanner rather than fight it mid-flash.
  devmode::pause();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  startScan();
  requestUpdate();
}

void WifiScannerActivity::onExit() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_MODE_NULL);
  devmode::resume();
  Activity::onExit();
}

// ---- Scan -------------------------------------------------------------------

void WifiScannerActivity::startScan() {
  state = SCANNING;
  networks.clear();
  selectorIndex = 0;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
}

void WifiScannerActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  networks.clear();
  if (result > 0) {
    networks.reserve(static_cast<size_t>(result));
    for (int i = 0; i < result; i++) {
      Network net;
      net.ssid = WiFi.SSID(i).c_str();
      if (net.ssid.empty()) net.ssid = "(hidden)";
      net.rssi = WiFi.RSSI(i);
      net.channel = WiFi.channel(i);
      net.encType = WiFi.encryptionType(i);

      uint8_t* bssid = WiFi.BSSID(i);
      char buf[20];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4],
               bssid[5]);
      net.bssid = buf;

      networks.push_back(std::move(net));
    }
    sortNetworks();
    analyzeChannels();
  }
  WiFi.scanDelete();
  state = LIST;
  requestUpdate();
}

void WifiScannerActivity::sortNetworks() {
  if (sortBySignal) {
    std::sort(networks.begin(), networks.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  } else {
    std::sort(networks.begin(), networks.end(), [](const Network& a, const Network& b) { return a.ssid < b.ssid; });
  }
}

void WifiScannerActivity::saveToCsv() {
  Storage.mkdir("/crossplay");
  Storage.mkdir("/crossplay/wifi-scanner");
  char filename[64];
  snprintf(filename, sizeof(filename), "/crossplay/wifi-scanner/scan_%lu.csv", millis());

  String csv = "SSID,BSSID,RSSI,Channel,Encryption\n";
  for (const auto& net : networks) {
    csv += String(net.ssid.c_str()) + "," + net.bssid.c_str() + "," + String(net.rssi) + "," + String(net.channel) +
           "," + encryptionString(net.encType) + "\n";
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

  int32_t found = -200;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      uint8_t* bssidBytes = WiFi.BSSID(i);
      if (!bssidBytes) continue;
      char bssidStr[20];
      snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X", bssidBytes[0], bssidBytes[1], bssidBytes[2],
               bssidBytes[3], bssidBytes[4], bssidBytes[5]);
      if (targetBssid == bssidStr) {
        found = WiFi.RSSI(i);
        break;
      }
    }
  }
  WiFi.scanDelete();

  if (found == -200) {
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
  rssiHistoryIndex = (rssiHistoryIndex + 1) % RSSI_HISTORY_SIZE;
  if (rssiHistoryCount < RSSI_HISTORY_SIZE) rssiHistoryCount++;

  requestUpdate();
}

// ---- Channel analysis -------------------------------------------------------

void WifiScannerActivity::analyzeChannels() {
  for (int ch = 1; ch <= 13; ch++) {
    channelData[ch].apCount = 0;
    channelData[ch].strongestRssi = -100;
    channelData[ch].avgRssi = -100;
    channelData[ch].rssiSum = 0;
    channelData[ch].interferenceScore = 0;
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
      channelData[ch].avgRssi = static_cast<int32_t>(channelData[ch].rssiSum / channelData[ch].apCount);
    }
  }

  for (const auto& net : networks) {
    int apCh = static_cast<int>(net.channel);
    if (apCh < 1 || apCh > 13) continue;
    int signalWeight = 100 + net.rssi;
    if (signalWeight < 1) signalWeight = 1;
    for (int target = 1; target <= 13; target++) {
      int dist = apCh - target;
      if (dist < 0) dist = -dist;
      if (dist > 2) continue;
      int overlapPct = (dist == 0) ? 100 : (dist == 1 ? 70 : 30);
      channelData[target].interferenceScore += signalWeight * overlapPct / 100;
    }
  }

  recommendedChannel = findBestChannel();
}

int WifiScannerActivity::findBestChannel() const {
  static const int preferred[] = {1, 6, 11};
  int bestScore = INT32_MAX;
  int bestCh = 1;

  for (int p : preferred) {
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
  if (state == SCANNING) {
    processScanResults();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
    }
    return;
  }

  if (state == DETAIL) {
    int tx = 0, ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tx, ty)) {
      state = LIST;
      requestUpdate();
    }
    return;
  }

  // ---- SIGNAL_VIEW ----
  if (viewMode == SIGNAL_VIEW) {
    unsigned long now = millis();
    if (now - lastMeasureTime >= MEASURE_INTERVAL_MS) {
      lastMeasureTime = now;
      doMeasurement();
    }
    int stx = 0, sty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(stx, sty)) {
      // Long-press cycled us in; a short press or a tap exits back to the list.
      viewMode = LIST_VIEW;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
    }
    return;
  }

  // ---- CHANNEL_VIEW ----
  if (viewMode == CHANNEL_VIEW) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    int ctx = 0, cty = 0;
    if (mappedInput.wasScreenTapped(ctx, cty)) {
      viewMode = LIST_VIEW;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startScan();
      viewMode = LIST_VIEW;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      channelViewMode = (channelViewMode == VIEW_SPECTRUM) ? VIEW_TABLE : VIEW_SPECTRUM;
      requestUpdate();
    }
    return;
  }

  // ---- LIST_VIEW ----
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
    if (mappedInput.getHeldTime() >= 500) {
      // Long press: cycle view modes
      if (viewMode == LIST_VIEW) {
        if (!networks.empty()) {
          targetIndex = selectorIndex;
          currentRssi = -100;
          minRssi = 0;
          maxRssi = -100;
          avgRssi = -100;
          sampleCount = 0;
          rssiSum = 0;
          rssiHistoryIndex = 0;
          rssiHistoryCount = 0;
          lastMeasureTime = 0;
          viewMode = SIGNAL_VIEW;
          requestUpdate();
        }
      } else if (viewMode == SIGNAL_VIEW) {
        channelViewMode = VIEW_SPECTRUM;
        viewMode = CHANNEL_VIEW;
        requestUpdate();
      } else {
        viewMode = LIST_VIEW;
        requestUpdate();
      }
    } else if (!networks.empty()) {
      detailIndex = selectorIndex;
      state = DETAIL;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    sortBySignal = !sortBySignal;
    sortNetworks();
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    startScan();
    requestUpdate();
  }

  // Touch: tap a network row to select it and open its details, using the same
  // row geometry the themed list draws with.
  if (!networks.empty()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int rowHeight = GUI.getMenuRowHeight(renderer);
    const int rowStep = rowHeight + metrics.menuSpacing;
    const int pageItems = std::max(1, contentHeight / std::max(1, rowStep));
    const int pageStart = (selectorIndex / pageItems) * pageItems;
    int touchedRow = -1;
    const auto rowTouch = mappedInput.rowTouch(touchedRow, contentTop, rowStep, std::min(pageItems, count - pageStart),
                                               0, renderer.getScreenWidth(), rowHeight);
    if (rowTouch != MappedInputManager::RowTouch::None && touchedRow >= 0) {
      const int index = pageStart + touchedRow;
      if (index < count) {
        selectorIndex = index;
        if (rowTouch == MappedInputManager::RowTouch::Tap) {
          detailIndex = index;
          state = DETAIL;
        }
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
}

// ---- Signal view render -----------------------------------------------------

void WifiScannerActivity::renderSignalView() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (targetIndex < 0 || targetIndex >= static_cast<int>(networks.size())) return;

  const Network& target = networks[targetIndex];

  char headerSub[32];
  snprintf(headerSub, sizeof(headerSub), "Ch %d | Signal Meter", static_cast<int>(target.channel));
  GUI.drawHeader(renderer,
                 Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                      static_cast<int16_t>(metrics.headerHeight)},
                 target.ssid.c_str(), headerSub);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
  const int leftPad = metrics.contentSidePadding;
  const int rightPad = metrics.contentSidePadding;

  char rssiText[16];
  snprintf(rssiText, sizeof(rssiText), "%d dBm", static_cast<int>(currentRssi));
  renderer.drawCenteredText(UI_12_FONT_ID, y, rssiText, true);
  y += renderer.getTextHeight(UI_12_FONT_ID) + 6;

  renderer.drawCenteredText(UI_10_FONT_ID, y, qualityLabel(currentRssi));
  y += renderer.getTextHeight(UI_10_FONT_ID) + 12;

  const int barMaxWidth = pageWidth - leftPad - rightPad;
  const int barHeight = 28;
  const int filledWidth = static_cast<int>(rssiToFraction(currentRssi) * static_cast<float>(barMaxWidth));
  renderer.drawRect(leftPad, y, barMaxWidth, barHeight, true);
  if (filledWidth > 2) {
    renderer.fillRect(leftPad + 1, y + 1, filledWidth - 2, barHeight - 2, true);
  }
  y += barHeight + 12;

  char statsText[48];
  if (sampleCount > 0) {
    snprintf(statsText, sizeof(statsText), "Min: %d  Avg: %d  Max: %d", static_cast<int>(minRssi),
             static_cast<int>(avgRssi), static_cast<int>(maxRssi));
  } else {
    snprintf(statsText, sizeof(statsText), "Waiting for samples...");
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, statsText);
  y += renderer.getTextHeight(UI_10_FONT_ID) + 16;

  // Line graph (matching biscuit)
  if (rssiHistoryCount >= 2) {
    const int graphLeft = leftPad;
    const int graphWidth = pageWidth - leftPad - rightPad;
    const int graphBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
    const int graphTop = y;
    const int graphHeight = graphBottom - graphTop;

    if (graphHeight > 10) {
      renderer.drawRect(graphLeft, graphTop, graphWidth, graphHeight, true);

      int prevX = -1, prevY = -1;
      for (int i = 0; i < rssiHistoryCount; i++) {
        int slot;
        if (rssiHistoryCount < RSSI_HISTORY_SIZE) {
          slot = i;
        } else {
          slot = (rssiHistoryIndex + i) % RSSI_HISTORY_SIZE;
        }
        const float frac = rssiToFraction(rssiHistory[slot]);
        const int px = graphLeft + 1 + (i * (graphWidth - 2)) / (rssiHistoryCount - 1);
        const int py = graphTop + 1 + static_cast<int>((1.0f - frac) * static_cast<float>(graphHeight - 2));

        if (prevX >= 0) renderer.drawLine(prevX, prevY, px, py, true);
        prevX = px;
        prevY = py;
      }
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "List View", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- Channel spectrum render ------------------------------------------------

void WifiScannerActivity::renderChannelSpectrum() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  char subtitle[40];
  snprintf(subtitle, sizeof(subtitle), "%d APs  Best: Ch %d", static_cast<int>(networks.size()), recommendedChannel);
  GUI.drawHeader(renderer,
                 Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                      static_cast<int16_t>(metrics.headerHeight)},
                 "Channel Analyzer", subtitle);

  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  const int graphX = 30;
  const int graphY = headerBottom + 20;
  const int graphW = pageWidth - 50;
  const int graphH = 250;

  renderer.drawLine(graphX, graphY + graphH, graphX + graphW, graphY + graphH, true);
  renderer.drawLine(graphX, graphY, graphX, graphY + graphH, true);

  renderer.drawText(SMALL_FONT_ID, 0, graphY, "-30");
  renderer.drawText(SMALL_FONT_ID, 0, graphY + graphH / 2 - renderer.getTextHeight(SMALL_FONT_ID) / 2, "-65");
  renderer.drawText(SMALL_FONT_ID, 0, graphY + graphH - renderer.getTextHeight(SMALL_FONT_ID), "-100");

  const int chSpacing = graphW / 13;
  auto chToX = [&](int ch) -> int { return graphX + (ch - 1) * chSpacing + chSpacing / 2; };

  // Dotted vertical grid
  for (int ch = 1; ch <= 13; ch++) {
    int cx = chToX(ch);
    for (int py = graphY; py < graphY + graphH; py += 4) {
      renderer.drawPixel(cx, py, true);
    }
  }

  // Recommended channel highlight box
  {
    int rcx = chToX(recommendedChannel);
    renderer.drawRect(rcx - chSpacing / 2, graphY, chSpacing, graphH, true);
  }

  // X-axis channel labels
  const int labelY = graphY + graphH + 3;
  for (int ch = 1; ch <= 13; ch++) {
    char label[4];
    snprintf(label, sizeof(label), "%d", ch);
    int tw = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, chToX(ch) - tw / 2, labelY, label);
  }

  // Bell curves per AP (2.4 GHz channel overlap model)
  for (const auto& net : networks) {
    int apCh = static_cast<int>(net.channel);
    if (apCh < 1 || apCh > 13) continue;

    int32_t rssiClamped = net.rssi;
    if (rssiClamped > -30) rssiClamped = -30;
    if (rssiClamped < -100) rssiClamped = -100;

    int peakH = (rssiClamped + 100) * graphH / 70;
    int centerX = chToX(apCh);
    int halfSpread = 2 * chSpacing;

    int prevPx = -1, prevPy = -1;
    for (int dx = -halfSpread; dx <= halfSpread; dx++) {
      int px = centerX + dx;
      if (px < graphX || px > graphX + graphW) {
        prevPx = -1;
        continue;
      }

      float normDist = static_cast<float>(dx < 0 ? -dx : dx) / static_cast<float>(chSpacing);
      float factor;
      if (normDist <= 0.5f) {
        factor = 1.0f;
      } else if (normDist <= 1.5f) {
        factor = 0.7f;
      } else {
        factor = 0.7f * (2.0f - normDist) / 0.5f;
        if (factor < 0.0f) factor = 0.0f;
      }

      int h = static_cast<int>(peakH * factor);
      int py = graphY + graphH - h;

      if (prevPx != -1) renderer.drawLine(prevPx, prevPy, px, py, true);
      prevPx = px;
      prevPy = py;
    }
  }

  // Interference bar chart below spectrum
  const int barY = graphY + graphH + 25;
  const int barMaxH = 40;
  const int barW = chSpacing;

  int maxInterference = 1;
  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].interferenceScore > maxInterference) maxInterference = channelData[ch].interferenceScore;
  }

  for (int ch = 1; ch <= 13; ch++) {
    int cx = chToX(ch);
    int bx = cx - barW / 2 + 1;
    int bw = barW - 2;
    int bh = (channelData[ch].interferenceScore * barMaxH) / maxInterference;
    if (bh < 1 && channelData[ch].interferenceScore > 0) bh = 1;
    if (bh > 0) renderer.fillRect(bx, barY + barMaxH - bh, bw, bh, true);
    if (ch == recommendedChannel) {
      renderer.drawRect(bx - 1, barY, bw + 2, barMaxH, true);
    }
  }

  const int recY = barY + barMaxH + 10;
  char recBuf[40];
  snprintf(recBuf, sizeof(recBuf), "Recommended: Channel %d", recommendedChannel);
  renderer.drawCenteredText(UI_10_FONT_ID, recY, recBuf, true);

  // Bounds check: if chart overflows, skip the bottom row
  if (recY + renderer.getTextHeight(UI_10_FONT_ID) < pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing) {
    const auto labels = mappedInput.mapLabels("Back", "Rescan", "Table", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

// ---- Channel table render ---------------------------------------------------

void WifiScannerActivity::renderChannelTable() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                      static_cast<int16_t>(metrics.headerHeight)},
                 "Channel Analyzer", "Table View");

  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int pad = metrics.contentSidePadding;

  const int colCh = pad;
  const int colAps = colCh + 42;
  const int colBest = colAps + 44;
  const int colAvg = colBest + 58;
  const int colBar = colAvg + 54;
  const int barMaxW = pageWidth - colBar - pad;

  const int rowH = renderer.getTextHeight(SMALL_FONT_ID) + 6;
  int y = headerBottom + 8;

  renderer.drawText(SMALL_FONT_ID, colCh, y, "Ch", true);
  renderer.drawText(SMALL_FONT_ID, colAps, y, "APs", true);
  renderer.drawText(SMALL_FONT_ID, colBest, y, "Best", true);
  renderer.drawText(SMALL_FONT_ID, colAvg, y, "Avg", true);
  renderer.drawText(SMALL_FONT_ID, colBar, y, "Interf", true);
  y += rowH + 2;
  renderer.drawLine(pad, y, pageWidth - pad, y, true);
  y += 4;

  int maxInterference = 1;
  for (int ch = 1; ch <= 13; ch++) {
    if (channelData[ch].interferenceScore > maxInterference) maxInterference = channelData[ch].interferenceScore;
  }

  const int hintsTop = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  for (int ch = 1; ch <= 13; ch++) {
    if (y + rowH > hintsTop) break;

    const ChannelInfo& info = channelData[ch];
    const bool isRec = (ch == recommendedChannel);

    char chBuf[6];
    snprintf(chBuf, sizeof(chBuf), isRec ? ">%d" : "%d", ch);
    renderer.drawText(SMALL_FONT_ID, colCh, y, chBuf, true);

    char apsBuf[8];
    snprintf(apsBuf, sizeof(apsBuf), "%d", info.apCount);
    renderer.drawText(SMALL_FONT_ID, colAps, y, apsBuf);

    char bestBuf[8];
    if (info.apCount > 0)
      snprintf(bestBuf, sizeof(bestBuf), "%d", info.strongestRssi);
    else
      bestBuf[0] = '-', bestBuf[1] = '\0';
    renderer.drawText(SMALL_FONT_ID, colBest, y, bestBuf);

    char avgBuf[8];
    if (info.apCount > 0)
      snprintf(avgBuf, sizeof(avgBuf), "%d", info.avgRssi);
    else
      avgBuf[0] = '-', avgBuf[1] = '\0';
    renderer.drawText(SMALL_FONT_ID, colAvg, y, avgBuf);

    int bw = (info.interferenceScore * barMaxW) / maxInterference;
    if (bw < 1 && info.interferenceScore > 0) bw = 1;
    const int bh = rowH - 4;
    if (bw > 0) renderer.fillRect(colBar, y + 2, bw, bh, true);
    if (isRec) renderer.drawRect(colBar - 1, y + 1, barMaxW + 2, bh + 2, true);

    y += rowH;
  }

  const auto labels = mappedInput.mapLabels("Back", "Rescan", "Spectrum", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---- render -----------------------------------------------------------------

void WifiScannerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (state == SCANNING) {
    GUI.drawHeader(renderer,
                   Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                        static_cast<int16_t>(metrics.headerHeight)},
                   "WiFi Scanner");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning...");
    renderer.displayBuffer();
    return;
  }

  if (state == DETAIL && detailIndex < static_cast<int>(networks.size())) {
    const auto& net = networks[detailIndex];
    GUI.drawHeader(renderer,
                   Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                        static_cast<int16_t>(metrics.headerHeight)},
                   net.ssid.c_str());

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
    const int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "SSID", true);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, net.ssid.c_str());
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "BSSID", true);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, net.bssid.c_str());
    y += lineH;

    char rssiStr[24];
    snprintf(rssiStr, sizeof(rssiStr), "%d dBm  %s", net.rssi, signalBars(net.rssi));
    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Signal", true);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, rssiStr);
    y += lineH;

    char chStr[8];
    snprintf(chStr, sizeof(chStr), "%u", net.channel);
    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Channel", true);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, chStr);
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Encryption", true);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, encryptionString(net.encType));

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (viewMode == SIGNAL_VIEW) {
    renderSignalView();
    renderer.displayBuffer();
    return;
  }

  if (viewMode == CHANNEL_VIEW) {
    if (channelViewMode == VIEW_SPECTRUM)
      renderChannelSpectrum();
    else
      renderChannelTable();
    renderer.displayBuffer();
    return;
  }

  // LIST_VIEW
  char subtitle[48];
  snprintf(subtitle, sizeof(subtitle), "%d found | %s", static_cast<int>(networks.size()),
           sortBySignal ? "By Signal" : "By Name");
  GUI.drawHeader(renderer,
                 Rect{0, static_cast<int16_t>(metrics.topPadding), static_cast<int16_t>(pageWidth),
                      static_cast<int16_t>(metrics.headerHeight)},
                 "WiFi Scanner", subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (networks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No networks found");
  } else {
    GUI.drawList(
        renderer,
        Rect{0, static_cast<int16_t>(contentTop), static_cast<int16_t>(pageWidth), static_cast<int16_t>(contentHeight)},
        static_cast<int>(networks.size()), selectorIndex,
        [this](int index) -> std::string { return networks[index].ssid; },
        [this](int index) -> std::string {
          const auto& net = networks[index];
          return std::string(signalBars(net.rssi)) + " " + std::to_string(net.rssi) + "dBm  Ch" +
                 std::to_string(net.channel) + "  " + encryptionString(net.encType);
        });
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "Sort", "");
  GUI.drawButtonHints(renderer, labels.btn1, "Hold: Views", labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");

  renderer.displayBuffer();
}
