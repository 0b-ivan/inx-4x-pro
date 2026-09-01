#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifndef SIMULATOR
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#include "activity/Activity.h"
#include "activity/ActivityWithSubactivity.h"

class GBEmulator;

class GameBoyActivity final : public Activity {
 public:
  GameBoyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string romPath,
                  std::function<void()> onClose);
  ~GameBoyActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;
  bool prioritizesScreenTouch() const override { return true; }
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return true; }
  bool allowGlobalPowerRefresh() override { return false; }

 private:
  static constexpr int kGbWidth = 160;
  static constexpr int kGbHeight = 144;
  static constexpr int kScale = 3;
  static constexpr int kGameWidth = kGbWidth * kScale;
  static constexpr int kGameHeight = kGbHeight * kScale;
  static constexpr int kFrameBytes = (kGbWidth * kGbHeight) / 4;
  static constexpr int kBankSize = 0x4000;
  static constexpr int kBankCacheSlots = 8;
  static constexpr uint32_t kMaxRomSize = 8u * 1024u * 1024u;
  static constexpr unsigned long kExitHoldMs = 1200;
  static constexpr unsigned long kAutoSaveMs = 60000;
  static constexpr unsigned long kDisplayIntervalMs = 50;
  static constexpr unsigned long kTouchPulseMs = 100;

  std::string romPath_;
  std::string romName_;
  std::string savePath_;
  std::function<void()> onClose_;

  GBEmulator* emulator_ = nullptr;
  uint8_t* romData_ = nullptr;
  uint8_t* bank0_ = nullptr;
  uint8_t* bankCache_ = nullptr;
  int* bankCacheMap_ = nullptr;
  uint8_t* cgbVram1_ = nullptr;
  uint8_t* cgbWramExtra_ = nullptr;

  bool ready_ = false;
  bool firstFrame_ = true;
  bool inputSinceSave_ = false;
  uint32_t lastFrameHash_ = 0;
  uint32_t displayedFrames_ = 0;
  uint32_t consumedFrameSequence_ = 0;
  unsigned long exitChordStartedAt_ = 0;
  unsigned long lastAutoSaveAt_ = 0;
  unsigned long lastDisplayAt_ = 0;
  unsigned long lastSynchronousFrameAt_ = 0;
  uint8_t touchPulseMask_ = 0;
  unsigned long touchPulseUntil_ = 0;
  int previousOrientation_ = 0;

  std::atomic<uint8_t> currentInput_{0};
  std::atomic<uint32_t> publishedFrameSequence_{0};
  std::array<uint8_t, kFrameBytes> publishedFrame_{};
  std::array<uint8_t, kFrameBytes> renderFrame_{};
  bool backgroundEmulation_ = false;

#ifndef SIMULATOR
  std::atomic<TaskHandle_t> emulatorTaskHandle_{nullptr};
  SemaphoreHandle_t emulatorMutex_ = nullptr;
  portMUX_TYPE frameMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<bool> stopEmulatorTask_{false};
#endif

  bool initializeEmulator();
  bool loadRom();
  void cleanupEmulator();
  void renderLoading(const char* message);
  void renderError(const char* message);
  void renderShell();
  void renderGameFrame();
  void drawTouchButton(int x, int y, int w, int h, const char* label) const;
  uint32_t hashFrame() const;
  uint8_t collectInput();
  void requestClose();

  void captureEmulatorFrame();
  bool snapshotLatestFrame();
  bool startEmulatorTask();
  void stopEmulatorTask();
#ifndef SIMULATOR
  static void emulatorTaskTrampoline(void* arg);
  void emulatorTaskLoop();
#endif

  bool loadSram();
  bool saveSram();
  static void* allocLarge(size_t size);
  static std::string basenameWithoutExtension(const std::string& path);
};

class GameBoyBrowserActivity final : public ActivityWithSubactivity {
 public:
  GameBoyBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack);

  void onEnter() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;
  bool prioritizesScreenTouch() const override { return true; }

 private:
  static constexpr const char* kRomDir = "/games/gb";
  static constexpr int kRowHeight = 58;
  static constexpr int kMaxRoms = 200;

  std::function<void()> onBack_;
  std::vector<std::string> roms_;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;

  void loadRoms();
  void render();
  void moveSelection(int delta);
  void launchSelected();
  int visibleRows() const;
  static bool isGameBoyRom(const std::string& name);
};
