#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
  static constexpr int kBankSize = 0x4000;
  static constexpr int kBankCacheSlots = 8;
  static constexpr uint32_t kMaxRomSize = 8u * 1024u * 1024u;
  static constexpr unsigned long kExitHoldMs = 1200;
  static constexpr unsigned long kAutoSaveMs = 60000;

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
  unsigned long exitChordStartedAt_ = 0;
  unsigned long lastAutoSaveAt_ = 0;
  uint8_t touchPulseMask_ = 0;
  uint8_t touchPulseFrames_ = 0;
  int previousOrientation_ = 0;

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
