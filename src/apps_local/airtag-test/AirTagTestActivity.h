#pragma once
#include <memory>
#include <Memory.h>

#include <BLEDevice.h>

#include <cstdint>
#include <string>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class AirTagTestActivity final : public Activity {
 public:
  explicit AirTagTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AirTagTest", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<AirTagTestActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum State { MODE_SELECT, SPOOFING };
  enum MacMode { STATIC_MAC, ROTATING_MAC };

  static constexpr int MODE_COUNT = 2;
  static constexpr const char* MODE_NAMES[] = {"Static MAC", "Rotating MAC"};

  State state = MODE_SELECT;
  MacMode macMode = STATIC_MAC;
  int modeIndex = 0;
  ButtonNavigator buttonNavigator;

  uint8_t currentMac[6] = {};
  uint32_t advCount = 0;
  unsigned long startTime = 0;
  unsigned long lastRotateTime = 0;
  unsigned long lastAdvTime = 0;

#ifdef SIMULATOR
  static constexpr unsigned long ROTATE_INTERVAL_MS = 2000;
#else
  static constexpr unsigned long ROTATE_INTERVAL_MS = 30000;
#endif
  static constexpr unsigned long ADV_INTERVAL_MS = 1000;

  BLEAdvertising* pAdvertising = nullptr;

  void startSpoofing(MacMode mode);
  void stopSpoofing();
  void generateRandomMac();
  void sendAirTagAdvertisement();
  std::string formatMac() const;
};
