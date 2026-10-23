#pragma once
// Initialize ESP32 FreeRTOS via Arduino.h before any IDF headers.
// Including portmacro.h directly first breaks the dual-core ESP32-S3 port.
#include <Arduino.h>
#include <memory>
#include <cstdint>
#include <vector>
#ifndef SIMULATOR
#include <esp_wifi.h>
#endif

#include <Memory.h>
#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class DeviceFingerprinterActivity final : public Activity {
 public:
  explicit DeviceFingerprinterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceFingerprinter", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<DeviceFingerprinterActivity>(r, m);
  }


  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return promiscuousActive; }

  // Called from promiscuous callback — must be public
  void onProbePacket(const uint8_t* payload, uint16_t len, int rssi);

 private:
  enum State { READY, CAPTURING };
  State state = READY;

  void startCapture();
  struct FingerprintedDevice {
    uint8_t mac[6];
    char estimatedOs[16];
    int probeCount;
    int8_t rssi;
  };

  static constexpr int MAX_DEVICES = 50;

  std::vector<FingerprintedDevice> devices;
  int deviceIndex = 0;
  ButtonNavigator buttonNavigator;

  bool promiscuousActive = false;
  unsigned long lastDisplay = 0;
  static constexpr unsigned long DISPLAY_INTERVAL_MS = 2000UL;

  portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

#ifndef SIMULATOR
  static DeviceFingerprinterActivity* activeInstance;
  static void promiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type);
#else
  bool simInjected = false;
  unsigned long simStarted = 0;
#endif

  static const char* estimateOs(const uint8_t* mac, int probeCount);
};
