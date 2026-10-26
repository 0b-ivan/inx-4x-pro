#pragma once
#include <memory>
#include <Memory.h>
#include <cstdint>
#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class QrTotpActivity final : public Activity {
 public:
  explicit QrTotpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QrTotp", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<QrTotpActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == SHOW_QR; }

 private:
  enum State { SELECT_ACCOUNT, SHOW_QR };
  State state = SELECT_ACCOUNT;

  struct Account {
    char name[32];
    char secret[65];
    uint8_t digits;
    uint8_t period;
  };

  static constexpr int MAX_ACCOUNTS = 16;
  static constexpr const char* SAVE_PATH = "/crossplay/totp.dat";

  Account accounts[MAX_ACCOUNTS];
  int accountCount = 0;
  int selectedIndex = 0;

  uint64_t lastPeriodIndex = 0;
  bool timeValid = false;

  void loadAccounts();

  static int base32Decode(const char* input, uint8_t* output, int outLen);
  static uint32_t generateTotp(const uint8_t* key, int keyLen, uint64_t counter, int digits);
  uint32_t currentCode() const;
};
