#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class WifiScannerActivity final : public Activity {
 public:
  WifiScannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WifiScanner", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class State : uint8_t { Scanning, List, Detail, SignalMeter, ChannelView };
  enum class ChannelViewMode : uint8_t { Spectrum, Table };

  struct Network {
    std::string ssid;
    std::string bssid;
    int32_t rssi;
    uint8_t channel;
    uint8_t encType;
  };

  struct ChannelInfo {
    int apCount = 0;
    int32_t strongestRssi = -100;
    int32_t avgRssi = -100;
    int64_t rssiSum = 0;
    int interferenceScore = 0;
  };

  State state = State::Scanning;
  ChannelViewMode channelViewMode = ChannelViewMode::Spectrum;

  std::vector<Network> networks;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool sortBySignal = true;

  // Signal meter
  int targetIndex = -1;
  int32_t currentRssi = -100;
  int32_t minRssi = 0;
  int32_t maxRssi = -100;
  int32_t avgRssi = -100;
  int sampleCount = 0;
  int64_t rssiSum = 0;
  static constexpr int kRssiHistorySize = 40;
  int32_t rssiHistory[kRssiHistorySize] = {};
  int rssiHistoryIndex = 0;
  int rssiHistoryCount = 0;
  unsigned long lastMeasureTime = 0;
  static constexpr unsigned long kMeasureIntervalMs = 500;

  // Channel analyser
  ChannelInfo channelData[14] = {};  // indices 1–13
  int recommendedChannel = 1;

  void startScan();
  void processScanResults();
  void sortNetworks();
  void saveToCsv();

  void doMeasurement();
  void analyzeChannels();
  int findBestChannel() const;

  static float rssiToFraction(int32_t rssi);
  static const char* qualityLabel(int32_t rssi);
  static const char* encryptionString(uint8_t type);
  static const char* signalBars(int32_t rssi);

  void renderList(GfxRenderer& r);
  void renderDetail(GfxRenderer& r);
  void renderSignalMeter(GfxRenderer& r);
  void renderChannelSpectrum(GfxRenderer& r);
  void renderChannelTable(GfxRenderer& r);
};
