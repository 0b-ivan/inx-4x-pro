#include "BleContactExchangeActivity.h"
#include "DevMode.h"
#include "../Shelf.h"

#ifndef SIMULATOR
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#else
#include "platform/sim_biscuit_ble.h"
#include <cstdio>
#include <cstdlib>
#endif
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RadioManager.h"

#ifdef SIMULATOR
static void simContactTrace(const char* state, size_t count) {
  const char* enabled = std::getenv("CROSSPLAY_SIM_BLE_TRACE");
  if (enabled && enabled[0] == '1' && enabled[1] == '\0')
    std::fprintf(stderr, "[BISCUIT_SIM] BLE_CONTACT:%s:%zu\n", state, count);
}
#endif
// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = IDLE;
  idleMenuIndex = 0;
  contactIndex = 0;
  received.clear();
  bleInitialized = false;
#ifdef SIMULATOR
  // Isolated demo persona: never read genuine host/user contact details.
  std::strncpy(myName, "Sim Operator", sizeof(myName) - 1);
  std::strncpy(myPhone, "TEST-SELF", sizeof(myPhone) - 1);
#else
  loadMyContact();
#endif
  requestUpdate();
}

void BleContactExchangeActivity::onExit() {
  // Shut the radio down before returning control to the developer-mode owner.
  if (bleInitialized) stopExchange();
  Activity::onExit();
  devmode::resume();
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::loadMyContact() {
  MyContactData data = {};
  auto file = Storage.open(MY_CONTACT_PATH);
  if (file) {
    file.read(reinterpret_cast<uint8_t*>(&data), sizeof(data));
    file.close();
    strncpy(myName,  data.name,  sizeof(myName)  - 1);
    strncpy(myPhone, data.phone, sizeof(myPhone) - 1);
    myName[sizeof(myName)   - 1] = '\0';
    myPhone[sizeof(myPhone) - 1] = '\0';
  }
}

void BleContactExchangeActivity::saveMyContact() {
  MyContactData data = {};
  strncpy(data.name,  myName,  sizeof(data.name)  - 1);
  strncpy(data.phone, myPhone, sizeof(data.phone) - 1);
  auto file = Storage.open(MY_CONTACT_PATH, O_WRITE | O_CREAT | O_TRUNC);
  if (file) {
    file.write(reinterpret_cast<const uint8_t*>(&data), sizeof(data));
    file.close();
  }
}

void BleContactExchangeActivity::saveContact(const Contact& contact) {
  auto file = Storage.open(CONTACTS_PATH, O_WRITE | O_CREAT | O_APPEND);
  if (file) {
    const std::string line = std::string(contact.name) + "," + contact.phone;
    file.println(line.c_str());
    file.close();
  }
}

// ---------------------------------------------------------------------------
// BLE exchange
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::startExchange() {
  RADIO.ensureBle();
  bleInitialized = true;

#ifndef SIMULATOR
  // Build manufacturer data: company_id(2) + name(14) + phone(13) = 29 bytes
  uint8_t mfgData[MFG_TOTAL] = {};
  mfgData[0] = BISCUIT_COMPANY_ID & 0xFF;
  mfgData[1] = (BISCUIT_COMPANY_ID >> 8) & 0xFF;
  strncpy(reinterpret_cast<char*>(mfgData + MFG_NAME_OFFSET),  myName,  MFG_NAME_LEN);
  strncpy(reinterpret_cast<char*>(mfgData + MFG_PHONE_OFFSET), myPhone, MFG_PHONE_LEN);

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  // Arduino BLE API expects String, not std::string
  String mfgStr;
  mfgStr.reserve(MFG_TOTAL);
  for (int i = 0; i < MFG_TOTAL; i++) mfgStr += static_cast<char>(mfgData[i]);
  advData.setManufacturerData(mfgStr);
  adv->setAdvertisementData(advData);
  adv->start();

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->start(0, nullptr, true);  // non-blocking continuous scan
#endif

  exchangeStart = millis();
  lastScanMs = millis();
  state = EXCHANGING;
#ifdef SIMULATOR
  simContactTrace("exchange", received.size());
#endif
}

void BleContactExchangeActivity::stopExchange() {
  if (!bleInitialized) return; // Idempotent cleanup on cancel, timeout or exit.
#ifndef SIMULATOR
  BLEDevice::getAdvertising()->stop();
  BLEDevice::getScan()->stop();
#endif
  RADIO.shutdown();
  bleInitialized = false;
#ifdef SIMULATOR
  simContactTrace(RADIO.getState() == RadioManager::RadioState::OFF
                    ? "radio-off" : "radio-leak", received.size());
#endif
}

void BleContactExchangeActivity::pollScanResults() {
#ifdef SIMULATOR
  const auto packets = biscuit_sim::bleContactPackets();
  for (const auto& packet : packets) {
    if (static_cast<int>(packet.size()) < MFG_TOTAL) continue;
    const char* mfg = packet.data();
#else
  BLEScanResults* pResults = BLEDevice::getScan()->getResults();
  if (!pResults) return;
  for (int i = 0; i < pResults->getCount(); i++) {
    BLEAdvertisedDevice dev = pResults->getDevice(i);
    if (!dev.haveManufacturerData()) continue;
    String mfgRaw = dev.getManufacturerData();
    if (static_cast<int>(mfgRaw.length()) < MFG_TOTAL) continue;
    const char* mfg = mfgRaw.c_str();
#endif
    uint16_t compId = static_cast<uint8_t>(mfg[0]) |
                      (static_cast<uint8_t>(mfg[1]) << 8);
    if (compId != BISCUIT_COMPANY_ID) continue;

    Contact c = {};
    memcpy(c.name,  mfg + MFG_NAME_OFFSET,  MFG_NAME_LEN);
    memcpy(c.phone, mfg + MFG_PHONE_OFFSET, MFG_PHONE_LEN);
    c.name[MFG_NAME_LEN]   = '\0';
    c.phone[MFG_PHONE_LEN] = '\0';

    // Skip empty / own contact
    if (c.name[0] == '\0') continue;
    if (strncmp(c.name, myName, MFG_NAME_LEN) == 0 &&
        strncmp(c.phone, myPhone, MFG_PHONE_LEN) == 0) continue;

    // Deduplicate by name
    bool found = false;
    for (const auto& existing : received) {
      if (strncmp(existing.name, c.name, MFG_NAME_LEN) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      received.push_back(c);
      requestUpdate();
    }
  }
#ifdef SIMULATOR
  simContactTrace("found", received.size());
#endif
}

// ---------------------------------------------------------------------------
// Keyboard helpers
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::editName() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Your Name",
                                              myName, sizeof(myName) - 1),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& text = std::get<KeyboardResult>(result.data).text;
          strncpy(myName, text.c_str(), sizeof(myName) - 1);
          myName[sizeof(myName) - 1] = '\0';
          // Chain into phone edit
          editPhone();
        }
      });
}

void BleContactExchangeActivity::editPhone() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Your Phone",
                                              myPhone, sizeof(myPhone) - 1),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& text = std::get<KeyboardResult>(result.data).text;
          strncpy(myPhone, text.c_str(), sizeof(myPhone) - 1);
          myPhone[sizeof(myPhone) - 1] = '\0';
          saveMyContact();
        }
      });
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::loop() {
  if (state == IDLE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }

    buttonNavigator.onNext([this] {
      idleMenuIndex = ButtonNavigator::nextIndex(idleMenuIndex, 2); requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      idleMenuIndex = ButtonNavigator::previousIndex(idleMenuIndex, 2); requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (idleMenuIndex == 0) { editName(); }
      else { if (myName[0] == '\0') { editName(); } else { startExchange(); requestUpdate(); } }
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
        if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
        if (btn == 2) {
          if (idleMenuIndex == 0) { editName(); }
          else { if (myName[0] == '\0') { editName(); } else { startExchange(); requestUpdate(); } }
          return;
        }
        int touchRow = -1;
        const auto& m = UITheme::getInstance().getMetrics();
        const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
        const int rowStep = GUI.getListRowStep(false);
        const auto rt = mappedInput.rowTouch(touchRow, contentTop, rowStep, 2, 0,
                                              renderer.getScreenWidth(), m.listRowHeight);
        if (rt == MappedInputManager::RowTouch::Tap && touchRow >= 0 && touchRow < 2) {
          idleMenuIndex = touchRow; requestUpdate();
        }
      }
    }
    return;
  }

  if (state == EXCHANGING) {
    unsigned long now = millis();

    if (now - lastScanMs >= SCAN_INTERVAL_MS) {
      lastScanMs = now;
      pollScanResults();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        (now - exchangeStart >= EXCHANGE_DURATION_MS)) {
      stopExchange();
      if (!received.empty()) { contactIndex = 0; state = RECEIVED; } else { state = IDLE; }
#ifdef SIMULATOR
      simContactTrace(state == RECEIVED ? "received" : "idle", received.size());
#endif
      requestUpdate();
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        if (GUI.tappedHintButton(tx_, ty_, renderer) == 1) {
          stopExchange();
          if (!received.empty()) { contactIndex = 0; state = RECEIVED; } else { state = IDLE; }
          requestUpdate();
        }
      }
    }
    return;
  }

  if (state == RECEIVED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = IDLE; idleMenuIndex = 0; requestUpdate();
#ifdef SIMULATOR
      simContactTrace("idle", received.size());
#endif
      return;
    }

    buttonNavigator.onNext([this] {
      contactIndex = ButtonNavigator::nextIndex(contactIndex, static_cast<int>(received.size())); requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      contactIndex = ButtonNavigator::previousIndex(contactIndex, static_cast<int>(received.size())); requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!received.empty()) {
        saveContact(received[contactIndex]);
        received.erase(received.begin() + contactIndex);
        if (received.empty()) { state = IDLE; idleMenuIndex = 0; } else { contactIndex = 0; }
        requestUpdate();
      }
    }
    {
      int tx_ = 0, ty_ = 0;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        const int btn = GUI.tappedHintButton(tx_, ty_, renderer);
        if (btn == 1) { state = IDLE; idleMenuIndex = 0; requestUpdate(); return; }
        if (btn == 2 && !received.empty()) {
          saveContact(received[contactIndex]);
          received.erase(received.begin() + contactIndex);
          if (received.empty()) { state = IDLE; idleMenuIndex = 0; } else { contactIndex = 0; }
          requestUpdate();
        }
      }
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void BleContactExchangeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case IDLE:       renderIdle();       break;
    case EXCHANGING: renderExchanging(); break;
    case RECEIVED:   renderReceived();   break;
  }
  renderer.displayBuffer();
}

void BleContactExchangeActivity::renderIdle() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Contact Exchange");

  const int contentTop = metrics.topPadding + metrics.headerHeight + 12;

  // Show current contact info
  char buf[48];
  int y = contentTop;

  renderer.drawText(SMALL_FONT_ID, 16, y, "My Info:");
  y += renderer.getLineHeight(SMALL_FONT_ID) + 4;

  if (myName[0] != '\0') {
    snprintf(buf, sizeof(buf), "Name:  %s", myName);
  } else {
    snprintf(buf, sizeof(buf), "Name:  (not set)");
  }
  renderer.drawText(UI_10_FONT_ID, 16, y, buf);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  if (myPhone[0] != '\0') {
    snprintf(buf, sizeof(buf), "Phone: %s", myPhone);
  } else {
    snprintf(buf, sizeof(buf), "Phone: (not set)");
  }
  renderer.drawText(UI_10_FONT_ID, 16, y, buf);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 20;

  // 2-item menu
  const int menuCount = 2;
  const char* labels[2] = {"Edit My Info", "Start Exchange"};

  GUI.drawList(renderer,
               Rect{0, y, pageWidth, pageHeight - y - metrics.buttonHintsHeight - 8},
               menuCount, idleMenuIndex,
               [&labels](int i) -> std::string { return labels[i]; });

  const auto hints = mappedInput.mapLabels("Back", "Select", "^", "v");
  GUI.drawButtonHintsAlways(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void BleContactExchangeActivity::renderExchanging() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Exchanging...");

  const int contentTop = metrics.topPadding + metrics.headerHeight + 20;

  // Progress bar (time-based)
  unsigned long elapsed = millis() - exchangeStart;
  if (elapsed > EXCHANGE_DURATION_MS) elapsed = EXCHANGE_DURATION_MS;
  int barW = pageWidth - 32;
  int filled = static_cast<int>((long)barW * (long)elapsed / (long)EXCHANGE_DURATION_MS);
  renderer.drawRect(16, contentTop, barW, 12, true);
  if (filled > 0) renderer.fillRect(16, contentTop, filled, 12, true);

  int y = contentTop + 24;

  // Countdown
  unsigned long remaining = (EXCHANGE_DURATION_MS - elapsed) / 1000 + 1;
  char countBuf[24];
  snprintf(countBuf, sizeof(countBuf), "Scanning... %lus", remaining);
  renderer.drawCenteredText(UI_10_FONT_ID, y, countBuf);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 16;

  // Received count
  char foundBuf[32];
  snprintf(foundBuf, sizeof(foundBuf), "Contacts found: %d", static_cast<int>(received.size()));
  renderer.drawCenteredText(UI_10_FONT_ID, y, foundBuf);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 24;

  // Live list of found contacts (up to 4)
  const int showMax = 4;
  int shown = static_cast<int>(received.size());
  if (shown > showMax) shown = showMax;
  for (int i = 0; i < shown; i++) {
    char cbuf[48];
    snprintf(cbuf, sizeof(cbuf), "+ %s", received[i].name);
    renderer.drawCenteredText(SMALL_FONT_ID, y, cbuf);
    y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
  }

  const auto hints = mappedInput.mapLabels("Stop", "", "", "");
  GUI.drawButtonHintsAlways(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void BleContactExchangeActivity::renderReceived() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth  = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 "Received Contacts");

  const int listTop    = metrics.topPadding + metrics.headerHeight + 4;
  const int listBottom = pageHeight - metrics.buttonHintsHeight - 8;

  const int count = static_cast<int>(received.size());
  GUI.drawList(renderer,
               Rect{0, listTop, pageWidth, listBottom - listTop},
               count, contactIndex,
               [this](int i) -> std::string { return received[i].name; },
               [this](int i) -> std::string { return received[i].phone; });

  const auto hints = mappedInput.mapLabels("Back", "Save", "^", "v");
  GUI.drawButtonHintsAlways(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}
