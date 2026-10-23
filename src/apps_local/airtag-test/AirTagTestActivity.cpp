#include "AirTagTestActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#ifndef SIMULATOR
#include <BLEDevice.h>
#include <esp_random.h>
#else
#include "platform/sim_biscuit_ble.h"
#include <cstdio>
#include <cstdlib>
#endif
#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../util/RadioManager.h"

constexpr const char* AirTagTestActivity::MODE_NAMES[];

#ifdef SIMULATOR
static void airtagSimTrace(const char* event, unsigned count, unsigned mode) {
  const char* enabled = std::getenv("CROSSPLAY_SIM_BLE_TRACE");
  if (enabled && enabled[0] == '1' && enabled[1] == '\0')
    std::fprintf(stderr,"[BISCUIT_SIM] AIRTAG:%s:%u:%u\n",event,count,mode);
}
#endif

void AirTagTestActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  RADIO.ensureBle();

  state = MODE_SELECT;
  modeIndex = 0;
  advCount = 0;
  startTime = 0;
  lastRotateTime = 0;
  lastAdvTime = 0;
  pAdvertising = nullptr;
  memset(currentMac, 0, sizeof(currentMac));

  requestUpdate();
}

void AirTagTestActivity::onExit() {
  stopSpoofing();
  RADIO.shutdown();
#ifdef SIMULATOR
  airtagSimTrace(RADIO.getState() == RadioManager::RadioState::OFF
                    ? "radio-off" : "radio-leak",advCount,macMode);
#endif
  Activity::onExit();
  devmode::resume();
}

void AirTagTestActivity::generateRandomMac() {
  for (int i = 0; i < 6; i++) {
#ifdef SIMULATOR
    currentMac[i] = static_cast<uint8_t>((advCount + 13u * i) & 0xff);
#else
    currentMac[i] = esp_random() & 0xFF;
#endif
  }
  // Set top two bits for random static address
  currentMac[0] |= 0xC0;
  // Random MAC is embedded in advertisement payload, not GAP address
}

std::string AirTagTestActivity::formatMac() const {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", currentMac[0], currentMac[1], currentMac[2],
           currentMac[3], currentMac[4], currentMac[5]);
  return std::string(buf);
}

void AirTagTestActivity::startSpoofing(MacMode mode) {
  macMode = mode;
  state = SPOOFING;
  advCount = 0;
  startTime = millis();
  lastRotateTime = millis();
  lastAdvTime = 0;

#ifndef SIMULATOR
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->stop();
#else
  airtagSimTrace("start", 0, macMode);
#endif
  generateRandomMac();
  requestUpdate();
}

void AirTagTestActivity::stopSpoofing() {
#ifndef SIMULATOR
  if (pAdvertising) {
    pAdvertising->stop();
    pAdvertising = nullptr;
  }
#else
  if (state == SPOOFING) airtagSimTrace("stop",advCount,macMode);
#endif
  state = MODE_SELECT;
}

void AirTagTestActivity::sendAirTagAdvertisement() {
#ifdef SIMULATOR
  // An intentionally non-wire-compatible preview, never a real Find My frame.
  const auto fake = biscuit_sim::blePreview(5, advCount++);
  airtagSimTrace("adv", advCount, fake.family);
  return;
#else
  if (!pAdvertising) return;

  pAdvertising->stop();

  BLEAdvertisementData advData;

  // Apple Find My network payload
  // Company ID 0x004C (Apple) + Find My type/length + public key payload
  uint8_t findMyPayload[] = {
      0x12, 0x19,  // Find My type and length
      0x10,        // Status byte
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00,  // First byte of public key bits
      0x00,  // Hint
  };

  // Fill simulated public key bytes with random data
  for (int i = 3; i < 25; i++) {
    findMyPayload[i] = esp_random() & 0xFF;
  }

  std::string mfData;
  // Apple company ID (little-endian)
  mfData += (char)0x4C;
  mfData += (char)0x00;
  mfData.append(reinterpret_cast<char*>(findMyPayload), sizeof(findMyPayload));

  advData.setManufacturerData(String(mfData.data(), mfData.size()));
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();

  advCount++;
#endif
}

void AirTagTestActivity::loop() {
  if (state == MODE_SELECT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startSpoofing(static_cast<MacMode>(modeIndex));
      return;
    }

    buttonNavigator.onNext([this] {
      modeIndex = ButtonNavigator::nextIndex(modeIndex, MODE_COUNT);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this] {
      modeIndex = ButtonNavigator::previousIndex(modeIndex, MODE_COUNT);
      requestUpdate();
    });

    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
        if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
        if (btn == 2) { startSpoofing(static_cast<MacMode>(modeIndex)); return; }
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(false);
        const int pageCount = MODE_COUNT;
        const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, pageCount, 0,
                                              renderer.getScreenWidth(), m.listRowHeight);
        if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < pageCount) {
          modeIndex = touchRow; requestUpdate();
        }
      }
    }
    return;
  }

  // SPOOFING state
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    stopSpoofing();
    requestUpdate();
    return;
  }
  {
    int tx_ = 0, ty_ = 0;
    if (mappedInput.wasScreenTapped(tx_, ty_)) {
      if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) { stopSpoofing(); requestUpdate(); return; }
    }
  }

  unsigned long now = millis();

  // Rotate MAC if in rotating mode
  if (macMode == ROTATING_MAC && (now - lastRotateTime >= ROTATE_INTERVAL_MS)) {
    lastRotateTime = now;
    generateRandomMac();
#ifdef SIMULATOR
    airtagSimTrace("rotated",advCount,macMode);
#endif
    LOG_DBG("AirTag", "Rotated MAC to %s", formatMac().c_str());
  }

  // Send advertisement periodically
  if (now - lastAdvTime >= ADV_INTERVAL_MS) {
    lastAdvTime = now;
    sendAirTagAdvertisement();
    requestUpdate();
  }
}

void AirTagTestActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "AirTag Spoofer");
#ifdef SIMULATOR
  renderer.drawCenteredText(SMALL_FONT_ID, metrics.topPadding + metrics.headerHeight + 2,
                            "OFFLINE SIMULATION - NO RADIO");
#endif

  if (state == MODE_SELECT) {
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, MODE_COUNT, modeIndex,
        [](int index) { return std::string(MODE_NAMES[index]); });

    const auto labels = mappedInput.mapLabels("Back", "Select", "^", "v");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // SPOOFING display
    const int centerY = pageHeight / 2 - 50;

    const char* modeName = (macMode == STATIC_MAC) ? "Static MAC" : "Rotating MAC";
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, modeName, true, EpdFontFamily::BOLD);

    // Current MAC address
    std::string macStr = "MAC: " + formatMac();
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 30, macStr.c_str());

    // Advertisement count
    char buf[64];
    snprintf(buf, sizeof(buf), "Advertisements: %lu", (unsigned long)advCount);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 55, buf);

    // Uptime
    unsigned long elapsed = (millis() - startTime) / 1000;
    snprintf(buf, sizeof(buf), "Uptime: %lum %lus", elapsed / 60, elapsed % 60);
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 80, buf);

    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
