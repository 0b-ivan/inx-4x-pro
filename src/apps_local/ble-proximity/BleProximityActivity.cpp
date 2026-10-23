#include "BleProximityActivity.h"
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

#ifndef SIMULATOR
// Radio callbacks must never run in the native SDL simulator.
static BleProximityActivity* activeProximity = nullptr;

class BleProximityCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!activeProximity) return;
    // Results are handled in the main loop via BLEScan results
  }
};

#endif  // !SIMULATOR

void BleProximityActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  devices.clear();
  selectorIndex = 0;
  scanning = false;
  scanInitialized = false;
  needsInit = true;
  requestUpdate();
}

void BleProximityActivity::onExit() {
  devmode::resume();
  Activity::onExit();
  if (scanInitialized) {
#ifndef SIMULATOR
    BLEScan* scan = BLEDevice::getScan();
    scan->stop();
#endif
    scanning = false;
  }
#ifndef SIMULATOR
  activeProximity = nullptr;
#endif
  if (scanInitialized) {
    RADIO.shutdown();
    scanInitialized = false;
  }
}

void BleProximityActivity::startBleScan() {
  if (!scanInitialized) return;
  scanning = true;
  lastScanTime = millis();

#ifndef SIMULATOR
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->clearResults();
  scan->start(0, nullptr, true);  // non-blocking
#endif
}

void BleProximityActivity::pruneStale() {
  const unsigned long now = millis();
  devices.erase(
      std::remove_if(devices.begin(), devices.end(),
                     [now](const BleTarget& d) {
                       return (now - d.lastSeen) > STALE_TIMEOUT_MS;
                     }),
      devices.end());
}

void BleProximityActivity::processScanResults() {
  // Not used — results are processed inline in loop() for clarity
}

void BleProximityActivity::loop() {
  if (needsInit) {
    needsInit = false;
    RADIO.ensureBle();
    scanInitialized = true;
#ifndef SIMULATOR
    activeProximity = this;
#endif
    startBleScan();
    requestUpdate();
    return;
  }

  if (scanning && scanInitialized) {
#ifdef SIMULATOR
    // In-memory BLE advertisements drive the real view, ordering and stale logic.
    // Polling cadence is synthetic; real controller timing needs device testing.
    if (millis() - lastScanTime >= 900) {
      const auto advertisements = biscuit_sim::bleAdvertisements();
      const unsigned long now = millis();
      for (auto& item : devices) item.active = false;
      for (const auto& advertisement : advertisements) {
        auto it = std::find_if(devices.begin(), devices.end(),
                               [&advertisement](const BleTarget& d) {
                                 return d.mac == advertisement.mac;
                               });
        if (it != devices.end()) {
          it->name = advertisement.name;
          it->rssi = advertisement.rssi;
          it->lastSeen = now;
          it->active = true;
        } else if (static_cast<int>(devices.size()) < MAX_DEVICES) {
          devices.push_back({advertisement.name, advertisement.mac,
                             advertisement.rssi, now, true});
        }
      }
      pruneStale();
      std::sort(devices.begin(), devices.end(),
                [](const BleTarget& a, const BleTarget& b) { return a.rssi > b.rssi; });
      if (!devices.empty() && selectorIndex >= static_cast<int>(devices.size())) {
        selectorIndex = static_cast<int>(devices.size()) - 1;
      }
      lastScanTime = now;
      requestUpdate();
    }
#else
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults* results = scan->getResults();
    const int count = results ? results->getCount() : 0;

    if (count > 0 || (millis() - lastScanTime > 4000)) {
      const unsigned long now = millis();

      // Mark all existing devices as inactive before merging
      for (auto& d : devices) {
        d.active = false;
      }

      // Merge results: update existing or add new
      for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results->getDevice(i);
        std::string mac = dev.getAddress().toString().c_str();

        auto it = std::find_if(devices.begin(), devices.end(),
                               [&mac](const BleTarget& d) { return d.mac == mac; });
        if (it != devices.end()) {
          it->rssi = dev.getRSSI();
          it->lastSeen = now;
          it->active = true;
          if (dev.haveName() && it->name == "Unknown") {
            it->name = dev.getName().c_str();
          }
        } else if (static_cast<int>(devices.size()) < MAX_DEVICES) {
          BleTarget newDev;
          newDev.name = dev.haveName() ? dev.getName().c_str() : "Unknown";
          newDev.mac = mac;
          newDev.rssi = dev.getRSSI();
          newDev.lastSeen = now;
          newDev.active = true;
          devices.push_back(std::move(newDev));
        }
      }

      // Remove devices not seen within the stale timeout
      pruneStale();

      // Sort by RSSI descending (strongest signal first)
      std::sort(devices.begin(), devices.end(),
                [](const BleTarget& a, const BleTarget& b) { return a.rssi > b.rssi; });

      // Clamp selector index to valid range
      const int devCount = static_cast<int>(devices.size());
      if (selectorIndex >= devCount && devCount > 0) {
        selectorIndex = devCount - 1;
      }

      scan->clearResults();
      requestUpdate();

      // Restart scan (non-blocking)
      lastScanTime = millis();
      scan->start(0, nullptr, true);
    }
#endif  // SIMULATOR
  }

  // Navigation
  const int devCount = static_cast<int>(devices.size());

  buttonNavigator.onNext([this, devCount] {
    if (devCount > 0) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, devCount);
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this, devCount] {
    if (devCount > 0) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, devCount);
      requestUpdate();
    }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
  }
  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) { shelf::leave(renderer, mappedInput); }
    }
  }
}

void BleProximityActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  if (needsInit) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "BLE Proximity");
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Starting BLE...");
    renderer.displayBuffer();
    return;
  }

  char subtitle[32];
  snprintf(subtitle, sizeof(subtitle), "%zu devices", devices.size());
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "BLE Proximity", subtitle);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (devices.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SCANNING_BLE));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight},
        static_cast<int>(devices.size()), selectorIndex,
        [this](int index) -> std::string {
          return devices[index].name;
        },
        [this](int index) -> std::string {
          char buf[64];
          snprintf(buf, sizeof(buf), "%s  %d dBm%s",
                   devices[index].mac.c_str(),
                   static_cast<int>(devices[index].rssi),
                   devices[index].active ? "" : " (stale)");
          return std::string(buf);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "^", "v");
  GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
