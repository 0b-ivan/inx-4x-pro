#include "WifiConnectActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "util/RadioManager.h"

#include <algorithm>
#include <map>

#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void WifiConnectActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  RADIO.ensureWifi();
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }
  selectedIndex = 0;
  networks.clear();
  state = SCANNING;
  requestUpdate();
  startScan();
}

void WifiConnectActivity::onExit() {
  devmode::resume();
  Activity::onExit();
  WiFi.scanDelete();
  // Keep WiFi connected on exit - do NOT disconnect
}

void WifiConnectActivity::startScan() {
  state = SCANNING;
  networks.clear();
  requestUpdate();
  // RadioManager already ensured WiFi mode in onEnter()
  WiFi.scanNetworks(true);
}

void WifiConnectActivity::processScanResults() {
  int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) return;

  if (result == WIFI_SCAN_FAILED || result <= 0) {
    state = LIST;
    requestUpdate();
    return;
  }

  std::map<std::string, ApInfo> unique;
  for (int i = 0; i < result; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    int32_t rssi = WiFi.RSSI(i);
    auto it = unique.find(ssid);
    if (it == unique.end() || rssi > it->second.rssi) {
      ApInfo ap;
      ap.ssid = ssid;
      ap.rssi = rssi;
      ap.encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      ap.saved = WIFI_STORE.hasSavedCredential(ssid);
      unique[ssid] = ap;
    }
  }

  networks.clear();
  for (auto& pair : unique) {
    networks.push_back(std::move(pair.second));
  }
  std::sort(networks.begin(), networks.end(), [](const ApInfo& a, const ApInfo& b) {
    if (a.saved != b.saved) return a.saved;
    return a.rssi > b.rssi;
  });

  WiFi.scanDelete();
  state = LIST;
  selectedIndex = 0;
  requestUpdate();
}

void WifiConnectActivity::selectNetwork(int index) {
  if (index < 0 || index >= static_cast<int>(networks.size())) return;
  auto& net = networks[index];
  selectedSSID = net.ssid;
  enteredPassword.clear();

  const auto saved = WIFI_STORE.findCredential(selectedSSID);
  if (saved && !saved->password.empty()) {
    enteredPassword = saved->password;
    attemptConnection();
    return;
  }

  if (net.encrypted) {
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "WiFi Password", "", 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            state = LIST;
          } else {
            enteredPassword = std::get<KeyboardResult>(result.data).text;
            attemptConnection();
          }
        });
  } else {
    attemptConnection();
  }
}

void WifiConnectActivity::attemptConnection() {
  state = CONNECTING;
  connectionStartTime = millis();
  connectedIP.clear();
  requestUpdate();

  WiFi.mode(WIFI_STA);
  if (!enteredPassword.empty()) {
    WiFi.begin(selectedSSID.c_str(), enteredPassword.c_str());
  } else {
    WiFi.begin(selectedSSID.c_str());
  }
}

void WifiConnectActivity::checkConnectionStatus() {
  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    connectedIP = buf;
    connectedRSSI = WiFi.RSSI();

    // Save credentials
    if (!enteredPassword.empty()) {
      RenderLock lock(*this);
      WIFI_STORE.addCredential(selectedSSID, enteredPassword);
      WIFI_STORE.setLastConnectedSsid(selectedSSID);
    }

    state = CONNECTED;
    requestUpdate();
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
      millis() - connectionStartTime > CONNECTION_TIMEOUT_MS) {
    WiFi.disconnect();
    state = LIST;
    requestUpdate();
  }
}

std::string WifiConnectActivity::signalBars(int32_t rssi) {
  if (rssi >= -50) return "||||";
  if (rssi >= -60) return " |||";
  if (rssi >= -70) return "  ||";
  return "   |";
}

void WifiConnectActivity::loop() {
  if (state == SCANNING) {
    if (millis() - lastSpinnerUpdate >= 600) {
      lastSpinnerUpdate = millis();
      spinnerFrame = (spinnerFrame + 1) % 3;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    processScanResults();
    return;
  }

  if (state == CONNECTING) {
    if (millis() - lastSpinnerUpdate >= 600) {
      lastSpinnerUpdate = millis();
      spinnerFrame = (spinnerFrame + 1) % 3;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      WiFi.disconnect();
      state = LIST;
      requestUpdate();
      return;
    }
    checkConnectionStatus();
    return;
  }

  if (state == CONNECTED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      shelf::leave(renderer, mappedInput);
    }
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) { shelf::leave(renderer, mappedInput); }
    return;
  }

  // LIST state
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (!networks.empty()) {
      selectNetwork(selectedIndex);
    } else {
      startScan();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    startScan();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, networks.size());
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, networks.size());
    requestUpdate();
  });

  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
      if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
      if (btn == 2) {
        if (!networks.empty()) selectNetwork(selectedIndex); else startScan(); return;
      }
      const int cnt = static_cast<int>(networks.size());
      if (cnt > 0) {
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int contentH = renderer.getScreenHeight() - contentTop - m.buttonHintsHeight - m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(true);
        const int pageItems = std::max(1, contentH / rowStep);
        const int pageStart = (selectedIndex / pageItems) * pageItems;
        const int pageCount = std::min(pageItems, cnt - pageStart);
        const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                              renderer.getScreenWidth(), m.listWithSubtitleRowHeight);
        if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
          selectedIndex = pageStart + touchRow; requestUpdate();
        }
      }
    }
  }
}

void WifiConnectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WIFI_CONNECT));

  switch (state) {
    case SCANNING:
      renderScanning();
      break;
    case LIST:
      renderList();
      break;
    case CONNECTING:
      renderConnecting();
      break;
    case CONNECTED:
      renderConnected();
      break;
  }

  renderer.displayBuffer();
}

void WifiConnectActivity::renderScanning() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "SCANNING...", spinnerFrame);
}

void WifiConnectActivity::renderList() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (networks.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No networks found");
  } else {
    int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(networks.size()), selectedIndex,
        [this](int i) { return networks[i].ssid; }, nullptr, nullptr,
        [this](int i) {
          return std::string(networks[i].saved ? "+ " : "") + (networks[i].encrypted ? "* " : "") +
                 signalBars(networks[i].rssi);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "Rescan");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void WifiConnectActivity::renderConnecting() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "CONNECTING...", spinnerFrame);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 50, selectedSSID.c_str());
}

void WifiConnectActivity::renderConnected() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto top = pageHeight / 2 - 50;
  renderer.drawCenteredText(UI_12_FONT_ID, top, "Connected", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, top + 35, selectedSSID.c_str());

  std::string ipLine = "IP: " + connectedIP;
  renderer.drawCenteredText(UI_10_FONT_ID, top + 65, ipLine.c_str());

  char sigBuf[32];
  snprintf(sigBuf, sizeof(sigBuf), "Signal: %d dBm %s", connectedRSSI, signalBars(connectedRSSI).c_str());
  renderer.drawCenteredText(UI_10_FONT_ID, top + 95, sigBuf);

  const auto labels = mappedInput.mapLabels("", tr(STR_DONE), "", "");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
