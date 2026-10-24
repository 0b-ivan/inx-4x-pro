// X4 Pro desktop simulator-only Fire Activity UI harness.
//
// The hardware FireActivity.cpp remains fully untouched for the ESP32 build.
// Native mode exercises the same Activity class, menus, confirmation and
// lifecycle, but NEVER implements, replays or transmits attack frames.
// There is no EAPOL capture, credential handling, radio, or live target input.
#ifdef SIMULATOR
#include "FireActivity.h"
#include "../Shelf.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void fireSimTrace(const char* event, int index, unsigned long tx) {
  const char* v = std::getenv("CROSSPLAY_SIM_FIRE_TRACE");
  if (v && v[0] == '1' && v[1] == '\0')
    std::fprintf(stderr, "[BISCUIT_SIM] FIRE:%s:%d:%lu\n", event, index, tx);
}

void FireActivity::setTarget(const uint8_t mac[6]) {
  if (!mac) return;
  std::memcpy(targetMac, mac, 6);
  hasPreselected = true;
  // Deliberately never query the host TargetDB.
}
void FireActivity::setAttack(int attackType) {
  preselectedAttack = attackType;
}
void FireActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  target = nullptr;
  targetCount = 0;
  packetsSent = 0;
  availableCount = 0;
  attackIndex = 0;
  pendingAttack = ATK_COUNT;
  state = ATTACK_SELECT;
  resultLine[0] = 0;
  statusLine[0] = 0;
  const struct {
    AttackType type; const char* label; const char* description; bool available;
  } menu[] = {
    {ATK_BLE_SPAM, "BLE Spam", "Synthetic preview; no BLE TX", true},
    {ATK_AIRTAG_SWARM, "AirTag Swarm", "Synthetic preview; no BLE TX", true},
    {ATK_BEACON_FLOOD, "Beacon Flood", "Synthetic preview; no WiFi TX", true},
    {ATK_KARMA_AP, "Karma AP", "Synthetic preview; no WLAN AP", true},
    {ATK_DEAUTH_BROADCAST, "Deauth Broadcast", "No live target in simulator", false},
    {ATK_DEAUTH_TARGETED, "Deauth Targeted", "No live target in simulator", false},
    {ATK_ROGUE_AP, "Rogue AP", "No live target in simulator", false},
    {ATK_HANDSHAKE_CAPTURE, "Handshake Capture", "Capture disabled", false},
    {ATK_PMKID_HARVEST, "PMKID Harvest", "Capture disabled", false},
    {ATK_EVIL_TWIN, "Evil Twin", "No real access point", false},
    {ATK_AUTH_FLOOD, "Auth Flood", "No radio transmissions", false},
    {ATK_BLE_CLONE, "BLE Clone", "No live BLE target", false},
    {ATK_BLE_ENUMERATE, "BLE Enumerate", "No GATT connection", false}
  };
  for (const auto& entry : menu) {
    if (availableCount >= ATK_COUNT) break;
    attacks[availableCount++] =
        {entry.type, entry.label, entry.description, entry.available};
  }
  if (preselectedAttack >= 0) {
    for (int i = 0; i < availableCount; ++i)
      if (static_cast<int>(attacks[i].type) == preselectedAttack)
        attackIndex = i;
    preselectedAttack = -1;
  }
  fireSimTrace("menu", attackIndex, packetsSent);
  requestUpdate();
}

void FireActivity::onExit() {
  // No virtual transport exists: packetsSent must remain zero for all states.
  packetsSent = 0;
  fireSimTrace("no-radio", attackIndex, packetsSent);
  Activity::onExit();
  devmode::resume();
}

void FireActivity::startAttack() {
  // Always a local *UI-only* flow. No BLE, no WiFi, no sockets.
  attackStartMs = millis();
  packetsSent = 0;
  std::snprintf(statusLine, sizeof(statusLine), "SIM ONLY - NO RF OUTPUT");
  state = EXECUTING;
  fireSimTrace("preview", attackIndex, packetsSent);
  requestUpdate();
}
void FireActivity::stopAttack() {
  packetsSent = 0;
  state = RESULTS;
  std::snprintf(resultLine, sizeof(resultLine), "No packets transmitted");
  fireSimTrace("results", attackIndex, packetsSent);
  requestUpdate();
}

void FireActivity::loop() {
  int x = 0, y = 0;
  const bool touch = mappedInput.wasScreenTapped(x, y);
  const int hint = touch ? GUI.tappedAlwaysHintButton(x, y, renderer) : 0;
  const bool confirm = mappedInput.wasReleased(MappedInputManager::Button::Confirm) || hint == 2;
  const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back) || hint == 1;

  if (state == ATTACK_SELECT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      attackIndex = (attackIndex + 1) % availableCount;
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      attackIndex = (attackIndex + availableCount - 1) % availableCount;
      requestUpdate();
    }
    if (touch && y > 110 && y < 700) {
      const int row = (y - 110) / 42;
      if (row >= 0 && row < availableCount) {
        attackIndex = row;
        fireSimTrace("touch-row", attackIndex, packetsSent);
        requestUpdate();
      }
    }
    if (back) { shelf::leave(renderer, mappedInput); return; }
    if (confirm && attacks[attackIndex].available) {
      state = CONFIRM;
      fireSimTrace("confirm", attackIndex, packetsSent);
      requestUpdate();
    }
  } else if (state == CONFIRM) {
    if (back) {
      state = ATTACK_SELECT;
      requestUpdate();
    } else if (confirm) {
      startAttack();
    }
  } else if (state == EXECUTING) {
    if (back || millis() - attackStartMs >= 2200) stopAttack();
  } else if (state == RESULTS) {
    if (back) {
      state = ATTACK_SELECT;
      fireSimTrace("back-menu", attackIndex, packetsSent);
      requestUpdate();
    } else if (confirm) {
      state = CONFIRM;
      requestUpdate();
    }
  }
}

void FireActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case TARGET_SELECT: renderTargetSelect(); break;
    case ATTACK_SELECT: renderAttackSelect(); break;
    case CONFIRM: renderConfirm(); break;
    case EXECUTING: renderExecuting(); break;
    case RESULTS: renderResults(); break;
  }
  renderer.displayBuffer();
}

void FireActivity::renderTargetSelect() const {
  GUI.drawHeader(renderer, Rect{0, 0, renderer.getScreenWidth(), 75}, "FIRE SIM: No live targets");
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight()/2,
                            "No physical targets available in simulator");
  GUI.drawButtonHintsAlways(renderer, "Back", "", "", "");
}
void FireActivity::renderAttackSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 "FIRE - SIMULATION", "No RF packets transmitted");
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = height - metrics.verticalSpacing - 80;
  GUI.drawList(renderer, Rect{0, top, width, bottom-top}, availableCount, attackIndex,
       [this](int i) -> std::string {
         return std::string(attacks[i].available ? "" : "[X] ") + attacks[i].name;
       },
       [this](int i) -> std::string { return attacks[i].desc; });
  GUI.drawButtonHintsAlways(renderer, "Exit", "Preview", "^", "v");
}
void FireActivity::renderConfirm() const {
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, 0, renderer.getScreenWidth(), 80},
                 "FIRE - SIMULATION", "No transmission");
  renderer.drawCenteredText(UI_12_FONT_ID, height/2 - 55, attacks[attackIndex].name);
  renderer.drawCenteredText(UI_10_FONT_ID, height/2 - 15,
                            "Confirm starts a local UI demonstration");
  renderer.drawCenteredText(SMALL_FONT_ID, height/2 + 35,
                            "No radio, no network, no actual targets");
  GUI.drawButtonHintsAlways(renderer, "Back", "Preview", "", "");
}
void FireActivity::renderExecuting() const {
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, 0, renderer.getScreenWidth(), 80},
                 "FIRE - SIMULATION", "No radio transmission");
  renderer.drawCenteredText(UI_12_FONT_ID, height/2 - 65,
                            attacks[attackIndex].name);
  renderer.drawCenteredText(UI_10_FONT_ID, height/2,
                            "LOCAL PREVIEW - RF OUTPUT DISABLED");
  renderer.drawCenteredText(UI_10_FONT_ID, height/2 + 35,
                            "Transmitted packets: 0");
  GUI.drawButtonHintsAlways(renderer, "Stop", "", "", "");
}
void FireActivity::renderResults() const {
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, 0, renderer.getScreenWidth(), 80},
                 "FIRE - SIMULATION", "Preview complete");
  renderer.drawCenteredText(UI_12_FONT_ID, height/2 - 55,
                            "No radio activity performed");
  renderer.drawCenteredText(UI_10_FONT_ID, height/2 + 10,
                            "Packets transmitted: 0");
  GUI.drawButtonHintsAlways(renderer, "Back", "Repeat", "", "");
}
#endif
