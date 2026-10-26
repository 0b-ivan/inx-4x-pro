#pragma once
#include <memory>
#include <Memory.h>
#include <cstdint>
#include "../../activities/Activity.h"

class SecurityPinActivity final : public Activity {
 public:
  explicit SecurityPinActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SecurityPin", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<SecurityPinActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  static bool isPinEnabled();

 private:
  enum State { ENTER_PIN, SET_PIN, SET_DURESS_PIN, SETTINGS_MENU };
  State state = ENTER_PIN;

  char pinBuffer[7] = {};
  int pinPos = 0;
  int pinLength = 4;

  uint8_t storedPinHash[32] = {};
  uint8_t storedDuressHash[32] = {};
  uint8_t flags = 0;
  int failCount = 0;
  static constexpr int MAX_FAILS = 5;

  int menuIndex = 0;
  static constexpr int MENU_ITEMS = 5;

  static constexpr const char* PIN_PATH = "/crossplay/security.dat";

  char newPin[7] = {};
  bool settingFirstEntry = true;

  unsigned long msgUntilMs = 0;
  char msgBuf[32] = {};

  void loadConfig();
  void saveConfig();
  static void hashPin(const char* pin, uint8_t* out);
  bool checkPin(const char* pin, const uint8_t* hash) const;
  void handlePinEntry();
  void handleSetPin();
  void handleSettingsMenu();
  void clearPinBuffer();
  void showMessage(const char* msg, unsigned long durationMs = 1200);

  void renderPinEntry() const;
  void renderSetPin() const;
  void renderSettingsMenu() const;
  void renderPinBoxes(int y, int length, int activePos, const char* digits) const;
};
