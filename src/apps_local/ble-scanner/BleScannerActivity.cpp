#include "BleScannerActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#ifndef SIMULATOR
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#else
#include "platform/sim_biscuit_ble.h"
#endif
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "util/RadioManager.h"

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#ifdef SIMULATOR
#include <cstdio>
#include <cstdlib>

// Emitted only for explicit UI regression runs; never available on hardware.
static void traceSimGatt(const char* state) {
  const char* enabled = std::getenv("CROSSPLAY_SIM_BLE_TRACE");
  if (enabled && enabled[0] == '1' && enabled[1] == '\0') {
    std::fprintf(stderr, "[BISCUIT_SIM] BLE_SCANNER:%s\n", state);
  }
}
#endif

#ifndef SIMULATOR
// Only real hardware uses asynchronous radio callbacks.
static BleScannerActivity* activeScanner = nullptr;

class BleScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!activeScanner) return;
    // Handled in main loop via BLEScan results
  }
};

#endif  // !SIMULATOR

// ---- lifecycle ----

void BleScannerActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = SCANNING_VIEW;
  devices.clear();
  selectorIndex = 0;
  scanning = false;
  scanInitialized = false;
  connected = false;
  pClient = nullptr;
  services.clear();
  characteristics.clear();
  selectedDevice = -1;
  selectedService = -1;
  selectedChar = -1;
  needsInit = true;
  requestUpdate();
}

void BleScannerActivity::onExit() {
  devmode::resume();
  Activity::onExit();
  disconnect();
  stopBleScan();
#ifndef SIMULATOR
  activeScanner = nullptr;
#endif
  if (scanInitialized) {
    RADIO.shutdown();
    scanInitialized = false;
  }
}

// ---- BLE scan helpers ----

void BleScannerActivity::startBleScan() {
  if (!scanInitialized) return;
  scanning = true;
  lastScanTime = millis();

#ifndef SIMULATOR
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->clearResults();
  scan->start(0, nullptr, true);  // non-blocking: scan once, return immediately
#endif
}

void BleScannerActivity::stopBleScan() {
  if (!scanInitialized) return;
#ifndef SIMULATOR
  BLEDevice::getScan()->stop();
#endif
  scanning = false;
}

void BleScannerActivity::saveToCsv() {
  Storage.mkdir("/crossplay/logs");
  char filename[64];
  snprintf(filename, sizeof(filename), "/crossplay/logs/ble_scan_%lu.csv", millis());

  String csv = "Name,MAC,RSSI\n";
  for (const auto& dev : devices) {
    csv += String(dev.name.c_str()) + "," + dev.mac.c_str() + "," + String(dev.rssi) + "\n";
  }
  Storage.writeFile(filename, csv);
  LOG_DBG("BSCAN", "Saved %zu devices to %s", devices.size(), filename);
}

// ---- connection helpers ----

void BleScannerActivity::connectToDevice(int index) {
  if (index < 0 || index >= static_cast<int>(devices.size())) return;
  selectedDevice = index;
  state = CONNECTING;
  requestUpdate(true);

  stopBleScan();

#ifdef SIMULATOR
  connected = biscuit_sim::bleConnects(devices[index].mac);
#else
  pClient = BLEDevice::createClient();
  connected = pClient->connect(devices[index].address, 0xFF, 5000);  // 5 second timeout instead of default 30
#endif

  if (connected) {
    enumerateServices();
    state = SERVICES;
    selectorIndex = 0;
#ifdef SIMULATOR
    traceSimGatt("services");
#endif
  } else {
#ifdef SIMULATOR
    traceSimGatt("connect-failed");
#endif
#ifndef SIMULATOR
    delete pClient;
#endif
    pClient = nullptr;
    state = SCANNING_VIEW;
#ifdef SIMULATOR
    startBleScan();
#endif
  }
  requestUpdate();
}

void BleScannerActivity::disconnect() {
#ifndef SIMULATOR
  if (pClient) {
    if (connected) pClient->disconnect();
    delete pClient;
    pClient = nullptr;
  }
#endif
  pClient = nullptr;
  connected = false;
  services.clear();
  characteristics.clear();
}

// ---- service / characteristic enumeration ----

void BleScannerActivity::enumerateServices() {
#ifdef SIMULATOR
  services.clear();
  if (!connected || selectedDevice < 0 ||
      selectedDevice >= static_cast<int>(devices.size())) return;
  const auto& mac = devices[selectedDevice].mac;
  for (const auto& service : biscuit_sim::bleGattServices(mac)) {
    ServiceInfo info;
    info.uuid = service.uuid;
    const char* resolved = resolveServiceName(info.uuid);
    info.name = resolved ? resolved : info.uuid;
    info.charCount = static_cast<int>(biscuit_sim::bleGattCharacteristics(mac, info.uuid).size());
    services.push_back(std::move(info));
  }
#else
  services.clear();
  if (!pClient || !connected) return;

  auto* svcMap = pClient->getServices();
  if (!svcMap) return;

  int count = 0;
  for (auto& pair : *svcMap) {
    if (count >= 20) break;
    ServiceInfo info;
    info.uuid = pair.first;
    const char* resolved = resolveServiceName(pair.first);
    if (resolved) {
      info.name = resolved;
    } else {
      info.name = (pair.first.size() >= 8) ? pair.first.substr(4, 4) : pair.first;
    }
    auto* charMap = pair.second->getCharacteristics();
    info.charCount = charMap ? static_cast<int>(charMap->size()) : 0;
    services.push_back(std::move(info));
    count++;
  }
#endif
}

void BleScannerActivity::enumerateCharacteristics(int serviceIndex) {
#ifdef SIMULATOR
  characteristics.clear();
  if (!connected || selectedDevice < 0 || selectedService < 0 ||
      serviceIndex < 0 || serviceIndex >= static_cast<int>(services.size())) return;
  const auto& mac = devices[selectedDevice].mac;
  for (const auto& value : biscuit_sim::bleGattCharacteristics(mac, services[serviceIndex].uuid)) {
    CharInfo info;
    info.uuid = value.uuid;
    const char* resolved = resolveCharName(info.uuid);
    info.name = resolved ? resolved : info.uuid;
    info.properties = 0;
    info.canRead = value.canRead;
    info.canWrite = value.canWrite;
    info.canNotify = value.canNotify;
    if (info.canRead) info.properties |= 0x02;
    if (info.canWrite) info.properties |= 0x08;
    if (info.canNotify) info.properties |= 0x10;
    info.value = info.canRead ? "(press OK to read)" : "(not readable)";
    characteristics.push_back(std::move(info));
  }
#else
  characteristics.clear();
  if (!pClient || !connected || serviceIndex < 0) return;

  auto* svcMap = pClient->getServices();
  if (!svcMap) return;

  int si = 0;
  for (auto& spair : *svcMap) {
    if (si == serviceIndex) {
      auto* charMap = spair.second->getCharacteristics();
      if (!charMap) return;
      int ci = 0;
      for (auto& cpair : *charMap) {
        if (ci >= 30) break;
        CharInfo info;
        info.uuid = cpair.first;
        const char* resolved = resolveCharName(cpair.first);
        info.name = resolved ? resolved : cpair.first;
        info.canRead   = cpair.second->canRead();
        info.canWrite  = cpair.second->canWrite() || cpair.second->canWriteNoResponse();
        info.canNotify = cpair.second->canNotify();
        info.properties = 0;
        if (info.canRead)   info.properties |= 0x02;
        if (info.canWrite)  info.properties |= 0x08;
        if (info.canNotify) info.properties |= 0x10;
        if (info.canRead) {
          info.value = "(press OK to read)";  // deferred: avoids blocking loop on enumerate
        } else {
          info.value = "(not readable)";
        }
        characteristics.push_back(std::move(info));
        ci++;
      }
      return;
    }
    si++;
  }
#endif
}

void BleScannerActivity::readCharacteristic(int charIndex) {
#ifdef SIMULATOR
  if (!connected || selectedDevice < 0 || selectedService < 0 ||
      selectedService >= static_cast<int>(services.size()) ||
      charIndex < 0 || charIndex >= static_cast<int>(characteristics.size())) return;
  const auto& mac = devices[selectedDevice].mac;
  const auto values = biscuit_sim::bleGattCharacteristics(mac, services[selectedService].uuid);
  if (charIndex >= static_cast<int>(values.size()) || !values[charIndex].canRead) return;
  const std::string raw = values[charIndex].value;
  std::string hex = bytesToHex(reinterpret_cast<const uint8_t*>(raw.data()),
                               static_cast<int>(raw.size()));
  std::string ascii = bytesToAsciiSafe(reinterpret_cast<const uint8_t*>(raw.data()),
                                       static_cast<int>(raw.size()));
  characteristics[charIndex].value = hex.empty() ? "(empty)" : hex;
  if (!ascii.empty()) characteristics[charIndex].value += " (" + ascii + ")";
#else
  if (!pClient || !connected || selectedService < 0) return;
  if (charIndex < 0 || charIndex >= static_cast<int>(characteristics.size())) return;

  auto* svcMap = pClient->getServices();
  if (!svcMap) return;

  // Walk to the selected service
  int si = 0;
  for (auto& spair : *svcMap) {
    if (si == selectedService) {
      auto* charMap = spair.second->getCharacteristics();
      if (!charMap) return;
      // Walk to the selected characteristic by index
      int ci = 0;
      for (auto& cpair : *charMap) {
        if (ci == charIndex) {
          if (!cpair.second->canRead()) return;
          String val = cpair.second->readValue();  // blocking read for this one char only
          CharInfo& info = characteristics[charIndex];
          if (val.length() > 0) {
            std::string hex = bytesToHex(
                reinterpret_cast<const uint8_t*>(val.c_str()),
                static_cast<int>(val.length()));
            std::string ascii = bytesToAsciiSafe(
                reinterpret_cast<const uint8_t*>(val.c_str()),
                static_cast<int>(val.length()));
            info.value = hex;
            if (!ascii.empty()) {
              info.value += " (";
              info.value += ascii;
              info.value += ")";
            }
          } else {
            info.value = "(empty)";
          }
          return;
        }
        ci++;
      }
      return;
    }
    si++;
  }
#endif
}

// ---- static helpers ----

const char* BleScannerActivity::resolveServiceName(const std::string& uuid) {
  if (uuid.size() < 8) return nullptr;
  std::string s = uuid.substr(4, 4);
  if (s == "1800") return "Generic Access";
  if (s == "1801") return "Generic Attribute";
  if (s == "180a") return "Device Information";
  if (s == "180f") return "Battery Service";
  if (s == "1812") return "HID Service";
  if (s == "181a") return "Environmental Sensing";
  if (s == "181c") return "User Data";
  if (s == "1810") return "Blood Pressure";
  if (s == "1808") return "Glucose";
  if (s == "1809") return "Health Thermometer";
  if (s == "180d") return "Heart Rate";
  if (s == "fee0") return "Vendor (Xiaomi)";
  if (s == "fe95") return "Vendor (Xiaomi Mi)";
  return nullptr;
}

const char* BleScannerActivity::resolveCharName(const std::string& uuid) {
  if (uuid.size() < 8) return nullptr;
  std::string s = uuid.substr(4, 4);
  if (s == "2a00") return "Device Name";
  if (s == "2a01") return "Appearance";
  if (s == "2a19") return "Battery Level";
  if (s == "2a24") return "Model Number";
  if (s == "2a25") return "Serial Number";
  if (s == "2a26") return "Firmware Rev";
  if (s == "2a27") return "Hardware Rev";
  if (s == "2a28") return "Software Rev";
  if (s == "2a29") return "Manufacturer";
  if (s == "2a37") return "Heart Rate Meas.";
  if (s == "2a6e") return "Temperature";
  if (s == "2a6f") return "Humidity";
  return nullptr;
}

std::string BleScannerActivity::propertiesToString(uint8_t props) {
  std::string result;
  if (props & 0x02)        result += "R";
  if (props & (0x08|0x04)) result += "W";
  if (props & 0x10)        result += "N";
  if (props & 0x20)        result += "I";
  return result;
}

std::string BleScannerActivity::bytesToHex(const uint8_t* data, int len) {
  char buf[4];
  std::string result;
  result.reserve(len <= 16 ? len * 3 : 16 * 3 + 3);
  int limit = len > 16 ? 16 : len;
  for (int i = 0; i < limit; i++) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    result += buf;
    if (i + 1 < limit) result += ' ';
  }
  if (len > 16) result += "...";
  return result;
}

std::string BleScannerActivity::bytesToAsciiSafe(const uint8_t* data, int len) {
  for (int i = 0; i < len; i++) {
    if (data[i] < 32 || data[i] > 126) return {};
  }
  return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
}

// ---- loop ----

void BleScannerActivity::loop() {
  if (needsInit) {
    if (millis() - lastSpinnerUpdate >= 600) {
      lastSpinnerUpdate = millis();
      spinnerFrame = (spinnerFrame + 1) % 3;
      requestUpdate();
    }
    needsInit = false;
    RADIO.ensureBle();
    scanInitialized = true;
#ifndef SIMULATOR
    activeScanner = this;
#endif
    startBleScan();
    requestUpdate();
    return;
  }

  if (state == DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = SCANNING_VIEW;
      requestUpdate();
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) { state = SCANNING_VIEW; requestUpdate(); }
      }
    }
    return;
  }

  if (state == CONNECTING) {
    // connectToDevice is synchronous; we should never linger here
    return;
  }

  if (state == SERVICES) {
    const int svcCount = static_cast<int>(services.size());

    buttonNavigator.onNext([this, svcCount] {
      if (svcCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, svcCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, svcCount] {
      if (svcCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, svcCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!services.empty()) {
        selectedService = selectorIndex;
        enumerateCharacteristics(selectorIndex);
        state = CHARACTERISTICS;
        selectorIndex = 0;
#ifdef SIMULATOR
        traceSimGatt("characteristics");
#endif
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      disconnect();
      state = SCANNING_VIEW;
      selectorIndex = selectedDevice >= 0 ? selectedDevice : 0;
      startBleScan();
#ifdef SIMULATOR
      traceSimGatt("back-scanning");
#endif
      requestUpdate();
    }
    return;
  }

  if (state == CHARACTERISTICS) {
    const int charCount = static_cast<int>(characteristics.size());

    buttonNavigator.onNext([this, charCount] {
      if (charCount > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, charCount); requestUpdate(); }
    });
    buttonNavigator.onPrevious([this, charCount] {
      if (charCount > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, charCount); requestUpdate(); }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!characteristics.empty()) {
        selectedChar = selectorIndex;
        if (characteristics[selectorIndex].canRead) {
          readCharacteristic(selectorIndex);
          if (selectedChar < static_cast<int>(characteristics.size())) {
            selectorIndex = selectedChar;
          }
        }
        state = CHAR_VALUE;
#ifdef SIMULATOR
        traceSimGatt("value");
#endif
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = SERVICES;
      selectorIndex = selectedService >= 0 ? selectedService : 0;
#ifdef SIMULATOR
      traceSimGatt("back-services");
#endif
      requestUpdate();
    }
    return;
  }

  if (state == CHAR_VALUE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size())) {
        readCharacteristic(selectedChar);
        if (selectedChar < static_cast<int>(characteristics.size())) {
          selectorIndex = selectedChar;
        }
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = CHARACTERISTICS;
      selectorIndex = selectedChar >= 0 ? selectedChar : 0;
#ifdef SIMULATOR
      traceSimGatt("back-characteristics");
#endif
      requestUpdate();
    }
    return;
  }

  // SCANNING_VIEW state

  // Advance spinner while waiting for first results
  if (devices.empty()) {
    if (millis() - lastSpinnerUpdate >= 600) {
      lastSpinnerUpdate = millis();
      spinnerFrame = (spinnerFrame + 1) % 3;
      requestUpdate();
    }
  }

  // Check if scan completed and collect results
#ifdef SIMULATOR
  if (scanning && scanInitialized && millis() - lastScanTime >= 900) {
    const auto advertised = biscuit_sim::bleAdvertisements();
    for (const auto& dev : advertised) {
      auto it = std::find_if(devices.begin(), devices.end(),
          [&dev](const BleDevice& existing) { return existing.mac == dev.mac; });
      if (it != devices.end()) {
        it->name = dev.name;
        it->rssi = dev.rssi;
      } else if (static_cast<int>(devices.size()) < MAX_DEVICES) {
        BleDevice entry;
        entry.name = dev.name;
        entry.mac = dev.mac;
        entry.rssi = dev.rssi;
        entry.address = BLEAddress(dev.mac);
        devices.push_back(std::move(entry));
      }
    }
    std::sort(devices.begin(), devices.end(),
        [](const BleDevice& a, const BleDevice& b) { return a.rssi > b.rssi; });
    if (!devices.empty() && selectorIndex >= static_cast<int>(devices.size()))
      selectorIndex = static_cast<int>(devices.size()) - 1;
    lastScanTime = millis();
    requestUpdate();
  }
#else
  if (scanning && scanInitialized) {
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults* results = scan->getResults();
    int count = results ? results->getCount() : 0;

    if (count > 0 || (millis() - lastScanTime > 4000)) {
      // Merge new results
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        std::string mac = dev.getAddress().toString().c_str();

        auto it = std::find_if(devices.begin(), devices.end(),
                               [&mac](const BleDevice& d) { return d.mac == mac; });
        if (it != devices.end()) {
          it->rssi = dev.getRSSI();
          if (dev.haveName() && it->name == "Unknown") {
            it->name = dev.getName().c_str();
          }
        } else if (static_cast<int>(devices.size()) < MAX_DEVICES) {
          BleDevice newDev;
          newDev.name = dev.haveName() ? dev.getName().c_str() : "Unknown";
          newDev.mac = mac;
          newDev.rssi = dev.getRSSI();
          newDev.address = dev.getAddress();
          devices.push_back(std::move(newDev));
        }
      }

      // Sort by RSSI
      std::sort(devices.begin(), devices.end(),
                [](const BleDevice& a, const BleDevice& b) { return a.rssi > b.rssi; });

      scan->clearResults();
      requestUpdate();

      // Restart scan (non-blocking)
      lastScanTime = millis();
      scan->start(0, nullptr, true);
    }
  }

#endif  // !SIMULATOR

  // Navigation
  const int count = static_cast<int>(devices.size());

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
    } else if (!devices.empty()) {
      connectToDevice(selectorIndex);
    }
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
      if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
      if (btn == 2 && !devices.empty()) { connectToDevice(selectorIndex); requestUpdate(); return; }
      if (count > 0) {
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int contentH = renderer.getScreenHeight() - contentTop - m.buttonHintsHeight - m.verticalSpacing;
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

// ---- render ----

void BleScannerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (needsInit) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_SCANNER));
    GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "STARTING...", spinnerFrame);
    renderer.displayBuffer();
    return;
  }

  if (state == CONNECTING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_BLE_SCANNER));
    GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "CONNECTING...", spinnerFrame);
    renderer.displayBuffer();
    return;
  }

  if (state == SERVICES) {
    const char* devName = (selectedDevice >= 0 && selectedDevice < static_cast<int>(devices.size()))
                              ? devices[selectedDevice].name.c_str()
                              : "Device";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   devName, "Services");

    const int contentTop    = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int svcCount      = static_cast<int>(services.size());

    if (svcCount > 0) {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, svcCount, selectorIndex,
          [this](int index) -> std::string { return services[index].name; },
          [this](int index) -> std::string {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d characteristics", services[index].charCount);
            return buf;
          });
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No services found");
    }

    const auto labels = mappedInput.mapLabels("Discon.", "Explore", "^", "v");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, "^", "v");
    renderer.displayBuffer();
    return;
  }

  if (state == CHARACTERISTICS) {
    const char* svcName = (selectedService >= 0 && selectedService < static_cast<int>(services.size()))
                              ? services[selectedService].name.c_str()
                              : "Service";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   svcName, "Characteristics");

    const int contentTop    = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int charCount     = static_cast<int>(characteristics.size());

    if (charCount > 0) {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, charCount, selectorIndex,
          [this](int index) -> std::string {
            std::string title = characteristics[index].name;
            title += " [";
            title += propertiesToString(characteristics[index].properties);
            title += "]";
            return title;
          },
          [this](int index) -> std::string {
            const std::string& val = characteristics[index].value;
            if (val.size() > 40) {
              return val.substr(0, 37) + "...";
            }
            return val;
          });
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "No characteristics");
    }

    const auto labels = mappedInput.mapLabels("Back", "Read", "^", "v");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(renderer, "^", "v");
    renderer.displayBuffer();
    return;
  }

  if (state == CHAR_VALUE) {
    const char* charName = (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size()))
                               ? characteristics[selectedChar].name.c_str()
                               : "Characteristic";
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   charName);

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 10;
    const int lineH = 45;

    if (selectedChar >= 0 && selectedChar < static_cast<int>(characteristics.size())) {
      const CharInfo& ch = characteristics[selectedChar];

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "UUID", true, EpdFontFamily::BOLD);
      y += 22;
      renderer.drawText(UI_10_FONT_ID, leftPad, y, ch.uuid.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Properties", true, EpdFontFamily::BOLD);
      y += 22;
      std::string props = propertiesToString(ch.properties);
      if (ch.canRead)   props += "  Read";
      if (ch.canWrite)  props += "  Write";
      if (ch.canNotify) props += "  Notify";
      renderer.drawText(UI_10_FONT_ID, leftPad, y, props.c_str());
      y += lineH;

      renderer.drawText(SMALL_FONT_ID, leftPad, y, "Value (hex)", true, EpdFontFamily::BOLD);
      y += 22;
      std::string hexPart = ch.value;
      auto parenPos = ch.value.find(" (");
      if (parenPos != std::string::npos) hexPart = ch.value.substr(0, parenPos);
      renderer.drawText(UI_10_FONT_ID, leftPad, y, hexPart.c_str());
      y += lineH;

      if (parenPos != std::string::npos && ch.value.size() > parenPos + 2) {
        std::string asciiPart = ch.value.substr(parenPos + 2);
        if (!asciiPart.empty() && asciiPart.back() == ')') asciiPart.pop_back();
        renderer.drawText(SMALL_FONT_ID, leftPad, y, "Value (ASCII)", true, EpdFontFamily::BOLD);
        y += 22;
        renderer.drawText(UI_10_FONT_ID, leftPad, y, asciiPart.c_str());
      }
    }

    const auto labels = mappedInput.mapLabels("Back", "Re-read", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == DETAIL && detailIndex < static_cast<int>(devices.size())) {
    const auto& dev = devices[detailIndex];
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_SCANNER));

    const int leftPad = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 20;
    const int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "Name", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, dev.name.c_str());
    y += lineH;

    renderer.drawText(SMALL_FONT_ID, leftPad, y, "MAC", true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, dev.mac.c_str());
    y += lineH;

    std::string rssiStr = std::to_string(dev.rssi) + " dBm";
    renderer.drawText(SMALL_FONT_ID, leftPad, y, tr(STR_RSSI), true, EpdFontFamily::BOLD);
    y += 22;
    renderer.drawText(UI_10_FONT_ID, leftPad, y, rssiStr.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // SCANNING_VIEW
  std::string subtitle = std::to_string(devices.size()) + " devices";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BLE_SCANNER),
                 subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (devices.empty()) {
    GUI.drawSpinner(renderer, pageWidth / 2, pageHeight / 2, "SCANNING...", spinnerFrame);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(devices.size()), selectorIndex,
        [this](int index) -> std::string { return devices[index].name; },
        [this](int index) -> std::string {
          return devices[index].mac + "  " + std::to_string(devices[index].rssi) + "dBm";
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "Connect", "^", "v");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, "Hold: CSV", labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, "^", "v");

  renderer.displayBuffer();
}
