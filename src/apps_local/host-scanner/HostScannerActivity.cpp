#include "HostScannerActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#ifndef SIMULATOR
#include <WiFiClient.h>
#else
#include "platform/sim_biscuit_network.h"
#endif

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

constexpr uint16_t HostScannerActivity::COMMON_PORTS[];

std::string HostScannerActivity::ipToString(uint32_t ip) const {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
  return buf;
}

void HostScannerActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = CHECKING_WIFI;
  hosts.clear();
  selectorIndex = 0;
  scanProgress = 0;
  selectedHost = -1;

  #ifdef SIMULATOR
  if (biscuit_sim::wifiConnected(WiFi.status() == WL_CONNECTED)) {
#else
  if (WiFi.isConnected()) {
#endif
    // This screen borrows an existing STA connection; it does not own the radio.
    startHostScan();
  }
  requestUpdate();
}

void HostScannerActivity::onExit() {
  // Do not disconnect a WLAN session established by WiFi Connect or Developer Mode.
  Activity::onExit();
  devmode::resume();
}

void HostScannerActivity::startHostScan() {
  state = SCANNING_HOSTS;
  hosts.clear();
  scanProgress = 0;

#ifdef SIMULATOR
  // A bounded isolated /27 fixture; do not inspect the host network.
  scanTotal = 30;
#else
  uint32_t localIp = WiFi.localIP();
  uint32_t mask = WiFi.subnetMask();
  scanTotal = (~mask & 0xFF) - 1;  // Number of host addresses
  if (scanTotal > 254) scanTotal = 254;
#endif
}

void HostScannerActivity::scanNextHost() {
  if (scanProgress >= scanTotal) {
    state = HOST_LIST;
    requestUpdate();
    return;
  }

  scanProgress++;
#ifdef SIMULATOR
  // IPs are stored in the same little-endian representation as WiFi.localIP().
  const uint32_t localIp = 0x0A0200C0U;  // 192.0.2.10
  const uint32_t targetIp = biscuit_sim::hostScanIp(scanProgress);
#else
  uint32_t localIp = WiFi.localIP();
  uint32_t mask = WiFi.subnetMask();
  uint32_t network = localIp & mask;
  uint32_t targetIp = network | (scanProgress & 0xFF);
#endif

  // Skip own IP
  if (targetIp == localIp) return;

  // Try TCP port 80, then 443, with a bounded connection timeout.
  // A fast refusal can suggest a live host, but duration alone cannot
  // distinguish TCP RST from all other connection errors.
#ifdef SIMULATOR
  const bool reachable = biscuit_sim::hostScanPortOpen(targetIp, 80)
                      || biscuit_sim::hostScanPortOpen(targetIp, 443);
#else
  IPAddress addr(targetIp);
  // Returns true if the host is reachable: either the port is open, or it
  // sent a TCP RST (fast failure < 100 ms) which also proves it is alive.
  auto tryPort = [](IPAddress& a, uint16_t port) -> bool {
    WiFiClient c;
    const unsigned long t0 = millis();
    // setTimeout() affects Stream reads, NOT the TCP connect deadline.
    const bool ok = c.connect(a, port, 180);
    const unsigned long elapsed = millis() - t0;
    c.stop();
    return ok || (WiFi.isConnected() && elapsed < 100);
  };
  const bool reachable = tryPort(addr, 80) || tryPort(addr, 443);
#endif
  if (reachable) {
    Host host;
    host.ip = targetIp;
    hosts.push_back(host);
    requestUpdate();
  }

  if (scanProgress % 10 == 0) requestUpdate();
}

void HostScannerActivity::startPortScan(int hostIndex) {
  selectedHost = hostIndex;
  portScanProgress = 0;
  hosts[hostIndex].openPorts.clear();
  state = PORT_SCANNING;
}

void HostScannerActivity::scanNextPort() {
  if (portScanProgress >= NUM_PORTS) {
    state = PORT_RESULTS;
    requestUpdate();
    return;
  }

  uint16_t port = COMMON_PORTS[portScanProgress];
  portScanProgress++;

#ifdef SIMULATOR
  const bool open = biscuit_sim::hostScanPortOpen(hosts[selectedHost].ip, port);
#else
  WiFiClient client;
  IPAddress addr(hosts[selectedHost].ip);
  const bool open = client.connect(addr, port, 500);
#endif
  if (open) {
#ifndef SIMULATOR
    client.stop();
#endif
    hosts[selectedHost].openPorts.push_back(port);
  }

  requestUpdate();
}

void HostScannerActivity::saveToCsv() {
  Storage.mkdir("/crossplay/scans");
  char filename[64];
  snprintf(filename, sizeof(filename), "/crossplay/scans/hostscan_%lu.csv", millis());

  String csv = "IP,Open Ports\n";
  for (const auto& host : hosts) {
    csv += ipToString(host.ip).c_str();
    csv += ",";
    for (size_t i = 0; i < host.openPorts.size(); i++) {
      if (i > 0) csv += ";";
      csv += String(host.openPorts[i]);
    }
    csv += "\n";
  }
  Storage.writeFile(filename, csv);
  LOG_DBG("HSCAN", "Saved to %s", filename);
}

void HostScannerActivity::loop() {
  int touchX = 0, touchY = 0;
  const bool touch = mappedInput.wasScreenTapped(touchX, touchY);
  const int hint = touch ? GUI.tappedAlwaysHintButton(touchX, touchY, renderer) : 0;
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back) || hint == 1;

  if (state == CHECKING_WIFI) {
#ifdef SIMULATOR
    if (!biscuit_sim::wifiConnected(WiFi.status() == WL_CONNECTED)) {
#else
    if (!WiFi.isConnected()) {
#endif
      if (back) shelf::leave(renderer, mappedInput);
    } else {
      startHostScan();
    }
    return;
  }

  if (state == SCANNING_HOSTS) {
    scanNextHost();
    if (back) {
      state = HOST_LIST;
      requestUpdate();
    }
    return;
  }

  if (state == PORT_SCANNING) {
    scanNextPort();
    if (back) {
      state = HOST_LIST;
      requestUpdate();
    }
    return;
  }

  if (state == PORT_RESULTS) {
    if (back) {
      state = HOST_LIST;
      requestUpdate();
    }
    return;
  }

  // HOST_LIST
  const int count = static_cast<int>(hosts.size());

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
      saveToCsv();
    } else if (!hosts.empty()) {
      startPortScan(selectorIndex);
    }
    requestUpdate();
  }

  if (back) { shelf::leave(renderer, mappedInput); return; }
  {
    if (touch) {
      const int btn = hint;
      if (btn == 2 && !hosts.empty()) { startPortScan(selectorIndex); requestUpdate(); return; }
      if (btn == 3) { saveToCsv(); requestUpdate(); return; }
      if (btn == 4) {
#ifdef SIMULATOR
        const bool connected = biscuit_sim::wifiConnected(WiFi.status() == WL_CONNECTED);
#else
        const bool connected = WiFi.isConnected();
#endif
        if (connected) startHostScan();
        else state = CHECKING_WIFI;
        requestUpdate(); return;
      }
      if (count > 0) {
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int contentH = renderer.getScreenHeight() - contentTop - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(false);
        const int pageItems = std::max(1, contentH / rowStep);
        const int pageStart = (selectorIndex / pageItems) * pageItems;
        const int pageCount = std::min(pageItems, count - pageStart);
        const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                              renderer.getScreenWidth(), m.listRowHeight);
        if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
          selectorIndex = pageStart + touchRow; requestUpdate();
        }
      }
    }
  }
}

void HostScannerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HOST_SCANNER));

  if (state == CHECKING_WIFI) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NOT_CONNECTED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SCANNING_HOSTS) {
    std::string progress = tr(STR_SCANNING_HOSTS) + std::string(" ") + std::to_string(scanProgress) + "/" +
                           std::to_string(scanTotal);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, progress.c_str());
    std::string found = std::to_string(hosts.size()) + " hosts found";
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, found.c_str());
    GUI.drawButtonHintsAlways(renderer, tr(STR_BACK), "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == PORT_SCANNING) {
    std::string ip = ipToString(hosts[selectedHost].ip);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, ip.c_str(), true, EpdFontFamily::BOLD);
    std::string progress =
        "Port " + std::to_string(portScanProgress) + "/" + std::to_string(NUM_PORTS);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, progress.c_str());
    std::string open = std::to_string(hosts[selectedHost].openPorts.size()) + " open";
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, open.c_str());
    GUI.drawButtonHintsAlways(renderer, tr(STR_BACK), "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state == PORT_RESULTS && selectedHost >= 0) {
    const auto& host = hosts[selectedHost];
    int y = metrics.topPadding + metrics.headerHeight + 30;
    const int leftPad = metrics.contentSidePadding;

    renderer.drawText(UI_10_FONT_ID, leftPad, y, ipToString(host.ip).c_str(), true, EpdFontFamily::BOLD);
    y += 40;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_OPEN_PORTS), true, EpdFontFamily::BOLD);
    y += 25;

    if (host.openPorts.empty()) {
      renderer.drawText(UI_10_FONT_ID, leftPad, y, "None");
    } else {
      for (uint16_t port : host.openPorts) {
        renderer.drawText(UI_10_FONT_ID, leftPad, y, std::to_string(port).c_str());
        y += 25;
      }
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // HOST_LIST
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - BaseMetrics::values.buttonHintsHeight - metrics.verticalSpacing;

  if (hosts.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_HOSTS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(hosts.size()), selectorIndex,
        [this](int i) -> std::string { return ipToString(hosts[i].ip); }, nullptr);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "CSV", "Rescan");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
