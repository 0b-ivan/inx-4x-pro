#include "GameBoyActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiTheme.h"
#include "util/StringUtils.h"

#ifndef SIMULATOR
#include <esp_heap_caps.h>
#endif

#include "../../../vendor/sumi/src/plugins/gb/gb_emulator.h"

namespace {
constexpr uint8_t kBayer4x4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
constexpr uint8_t kShadeIntensity[4] = {0, 5, 11, 16};
constexpr int kControlsTop = 442;
constexpr int kTitleY = 452;
constexpr int kDpadLeftX = 22;
constexpr int kDpadCenterX = 92;
constexpr int kDpadRightX = 162;
constexpr int kDpadUpY = 520;
constexpr int kDpadMiddleY = 586;
constexpr int kDpadDownY = 652;
constexpr int kControlSize = 58;
constexpr int kButtonBX = 300;
constexpr int kButtonBY = 600;
constexpr int kButtonAX = 388;
constexpr int kButtonAY = 540;
constexpr int kActionW = 74;
constexpr int kActionH = 58;
constexpr int kSelectX = 205;
constexpr int kStartX = 315;
constexpr int kMetaY = 718;
constexpr int kMetaW = 94;
constexpr int kMetaH = 44;
constexpr int kExitX = 22;
constexpr int kExitY = 718;
constexpr int kExitW = 120;
constexpr int kExitH = 44;

bool inRect(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

std::string truncatedLabel(const std::string& value, const size_t maxLen) {
  if (value.size() <= maxLen) return value;
  if (maxLen <= 3) return value.substr(0, maxLen);
  return value.substr(0, maxLen - 3) + "...";
}
}  // namespace

GameBoyActivity::GameBoyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string romPath,
                                 std::function<void()> onClose)
    : Activity("GameBoy", renderer, mappedInput), romPath_(std::move(romPath)), onClose_(std::move(onClose)) {
  romName_ = basenameWithoutExtension(romPath_);
  savePath_ = std::string("/games/saves/") + romName_ + ".sav";
}

void* GameBoyActivity::allocLarge(const size_t size) {
#ifndef SIMULATOR
  if (psramFound()) {
    if (void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
      return p;
    }
  }
#endif
  return malloc(size);
}

std::string GameBoyActivity::basenameWithoutExtension(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < begin ? path.size() : dot;
  return path.substr(begin, end - begin);
}

void GameBoyActivity::onEnter() {
  Activity::onEnter();
  previousOrientation_ = static_cast<int>(renderer.getOrientation());
  renderer.setOrientation(GfxRenderer::Portrait);
  mappedInput.setInvertDirectionalAxes180(false);

  SdMan.ensureDirectoryExists("/games");
  SdMan.ensureDirectoryExists("/games/gb");
  SdMan.ensureDirectoryExists("/games/saves");

  renderLoading("Loading Game Boy ROM...");
  if (!initializeEmulator()) {
    renderError("ROM could not be started");
    return;
  }

  emulator_->runFrames(2);
  captureEmulatorFrame();

  ready_ = true;
  firstFrame_ = true;
  lastAutoSaveAt_ = millis();
  lastDisplayAt_ = millis();
  lastSynchronousFrameAt_ = millis();
  currentInput_.store(0, std::memory_order_relaxed);

  renderShell();
  lastFrameHash_ = hashFrame();

  backgroundEmulation_ = startEmulatorTask();
  if (!backgroundEmulation_) {
    Serial.println("[GB] Background task unavailable; using synchronous frame pacing");
  }
}

void GameBoyActivity::onExit() {
  stopEmulatorTask();

  if (ready_) {
    saveSram();
  }

  ready_ = false;
  backgroundEmulation_ = false;
  cleanupEmulator();
  renderer.resetTransientReaderState();
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(previousOrientation_));
  mappedInput.setInvertDirectionalAxes180(false);
  Activity::onExit();
}

bool GameBoyActivity::initializeEmulator() {
  emulator_ = new (std::nothrow) GBEmulator();
  if (!emulator_) {
    Serial.println("[GB] Failed to allocate emulator object");
    return false;
  }

  if (!loadRom()) {
    Serial.println("[GB] ROM load failed");
    cleanupEmulator();
    return false;
  }

  const uint8_t* header = romData_ ? romData_ : bank0_;
  if (header) {
    const uint8_t cgbFlag = header[0x143];
    if (cgbFlag == 0x80 || cgbFlag == 0xC0) {
      cgbVram1_ = static_cast<uint8_t*>(allocLarge(0x2000));
      cgbWramExtra_ = static_cast<uint8_t*>(allocLarge(0x6000));
      if (cgbVram1_ && cgbWramExtra_) {
        emulator_->enableCgbMode(cgbVram1_, cgbWramExtra_);
      } else {
        Serial.println("[GB] CGB extra memory unavailable; continuing in DMG-compatible mode");
        if (cgbVram1_) free(cgbVram1_);
        if (cgbWramExtra_) free(cgbWramExtra_);
        cgbVram1_ = nullptr;
        cgbWramExtra_ = nullptr;
      }
    }

    if (pokered::isPokemonInBank0(header, kBankSize)) {
      Serial.println("[GB] Pokemon Red/Blue e-ink patches enabled");
      pokered::patchBank(const_cast<uint8_t*>(header), 0);
      emulator_->pokemonRedPatch_ = true;
    } else if (gbpatches::isPatchedGame(header, kBankSize)) {
      Serial.println("[GB] Per-game e-ink patches enabled");
      gbpatches::applyToBank(const_cast<uint8_t*>(header), 0, header, kBankSize);
      emulator_->gbPatchEnabled_ = true;
    }
  }

  if (!emulator_->init()) {
    Serial.println("[GB] Emulator buffers could not be allocated");
    cleanupEmulator();
    return false;
  }

#ifndef SIMULATOR
  emulatorMutex_ = xSemaphoreCreateMutex();
  if (!emulatorMutex_) {
    Serial.println("[GB] Failed to allocate emulator mutex");
    cleanupEmulator();
    return false;
  }
#endif

  loadSram();
  Serial.printf("[GB] Ready: %s, free heap=%u\n", romPath_.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

bool GameBoyActivity::loadRom() {
  FsFile file;
  if (!SdMan.openFileForRead("GB", romPath_.c_str(), file)) {
    return false;
  }

  const uint64_t rawSize = file.fileSize();
  if (rawSize < 0x150 || rawSize > kMaxRomSize) {
    Serial.printf("[GB] Invalid ROM size: %llu\n", static_cast<unsigned long long>(rawSize));
    file.close();
    return false;
  }
  const uint32_t romSize = static_cast<uint32_t>(rawSize);

  // X4 Pro has 8 MB PSRAM. Prefer a complete ROM image there because random
  // cartridge reads are vastly cheaper than SDMMC bank misses. If the ROM does
  // not fit, fall back to the same 16 KB bank-streaming model used by SumiBoy.
  romData_ = static_cast<uint8_t*>(allocLarge(romSize));
  if (romData_) {
    const int bytesRead = file.read(romData_, romSize);
    file.close();
    if (bytesRead == static_cast<int>(romSize)) {
      emulator_->romBank0_ = romData_;
      return emulator_->loadRom(romPath_.c_str(), romData_, romSize);
    }
    free(romData_);
    romData_ = nullptr;
  } else {
    file.close();
  }

  if (!SdMan.openFileForRead("GB", romPath_.c_str(), file)) {
    return false;
  }

  bank0_ = static_cast<uint8_t*>(allocLarge(kBankSize));
  bankCache_ = static_cast<uint8_t*>(allocLarge(kBankCacheSlots * kBankSize));
  bankCacheMap_ = static_cast<int*>(malloc(kBankCacheSlots * sizeof(int)));
  if (!bank0_ || !bankCache_ || !bankCacheMap_) {
    file.close();
    return false;
  }

  if (file.read(bank0_, kBankSize) != kBankSize) {
    file.close();
    return false;
  }
  file.close();

  for (int i = 0; i < kBankCacheSlots; ++i) bankCacheMap_[i] = -1;
  emulator_->setupBankCache(bank0_, bankCache_, kBankCacheSlots, bankCacheMap_);
  if (!SdMan.openFileForRead("GB", romPath_.c_str(), emulator_->romFile_)) {
    return false;
  }
  return emulator_->loadRom(romPath_.c_str(), nullptr, romSize);
}

void GameBoyActivity::cleanupEmulator() {
#ifndef SIMULATOR
  if (emulatorMutex_) {
    vSemaphoreDelete(emulatorMutex_);
    emulatorMutex_ = nullptr;
  }
#endif

  if (emulator_) {
    if (emulator_->romFile_) emulator_->romFile_.close();
    delete emulator_;
    emulator_ = nullptr;
  }
  if (romData_) free(romData_);
  if (bank0_) free(bank0_);
  if (bankCache_) free(bankCache_);
  if (bankCacheMap_) free(bankCacheMap_);
  if (cgbVram1_) free(cgbVram1_);
  if (cgbWramExtra_) free(cgbWramExtra_);
  romData_ = nullptr;
  bank0_ = nullptr;
  bankCache_ = nullptr;
  bankCacheMap_ = nullptr;
  cgbVram1_ = nullptr;
  cgbWramExtra_ = nullptr;
}

void GameBoyActivity::renderLoading(const char* message) {
  renderer.clearScreen();
  INX_THEME.drawPageHeader(renderer, "Game Boy");
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 180, message, true);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 215, "ROMs: /games/gb/", true);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void GameBoyActivity::renderError(const char* message) {
  renderer.clearScreen();
  INX_THEME.drawPageHeader(renderer, "Game Boy");
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 180, message, true);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 215, "Back or EXIT to return", true);
  drawTouchButton(kExitX, kExitY, kExitW, kExitH, "EXIT");
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void GameBoyActivity::drawTouchButton(const int x, const int y, const int w, const int h, const char* label) const {
  renderer.rectangle.render(x, y, w, h, true, true);
  const int lineHeight = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID);
  const int textY = y + std::max(2, (h - lineHeight) / 2);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, x + 10, textY, label, true);
}

void GameBoyActivity::renderShell() {
  renderer.clearScreen();
  snapshotLatestFrame();
  renderGameFrame();
  renderer.line.render(0, kControlsTop, renderer.getScreenWidth() - 1, kControlsTop, true);

  const std::string title = truncatedLabel(romName_, 30);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, kTitleY, title.c_str(), true);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 482,
                         "Confirm=A  Back=B  Side=Select/Start", true);

  drawTouchButton(kDpadCenterX, kDpadUpY, kControlSize, kControlSize, "UP");
  drawTouchButton(kDpadLeftX, kDpadMiddleY, kControlSize, kControlSize, "LEFT");
  drawTouchButton(kDpadRightX, kDpadMiddleY, kControlSize, kControlSize, "RIGHT");
  drawTouchButton(kDpadCenterX, kDpadDownY, kControlSize, kControlSize, "DOWN");
  drawTouchButton(kButtonBX, kButtonBY, kActionW, kActionH, "B");
  drawTouchButton(kButtonAX, kButtonAY, kActionW, kActionH, "A");
  drawTouchButton(kExitX, kExitY, kExitW, kExitH, "EXIT");
  drawTouchButton(kSelectX, kMetaY, kMetaW, kMetaH, "SELECT");
  drawTouchButton(kStartX, kMetaY, kMetaW, kMetaH, "START");

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 774, "Hold Confirm + Back to exit", true);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  firstFrame_ = false;
  displayedFrames_ = 1;
  lastDisplayAt_ = millis();
}

void GameBoyActivity::renderGameFrame() {
  const uint8_t* frame = renderFrame_.data();
  uint8_t* target = renderer.getFrameBuffer();
  if (!target) return;

  // The X4 Pro's native panel is 800x480. INX Portrait maps logical (x,y)
  // to native (y, 479-x). Derive the native stride from the live framebuffer
  // instead of hard-coding 100 bytes so the adapter remains defensive.
  if (renderer.getScreenWidth() != kGameWidth || renderer.getScreenHeight() < kGameHeight) {
    for (int gy = 0; gy < kGbHeight; ++gy) {
      for (int gx = 0; gx < kGbWidth; ++gx) {
        const int byteIndex = gy * (kGbWidth / 4) + (gx >> 2);
        const uint8_t shade = (frame[byteIndex] >> ((gx & 3) << 1)) & 0x03;
        const uint8_t intensity = kShadeIntensity[shade];
        for (int dy = 0; dy < kScale; ++dy) {
          for (int dx = 0; dx < kScale; ++dx) {
            const int x = gx * kScale + dx;
            const int y = gy * kScale + dy;
            renderer.drawPixel(x, y, intensity > kBayer4x4[y & 3][x & 3]);
          }
        }
      }
    }
    return;
  }

  const size_t nativeStride = renderer.getBufferSize() / static_cast<size_t>(renderer.getScreenWidth());
  for (int gy = 0; gy < kGbHeight; ++gy) {
    for (int gx = 0; gx < kGbWidth; ++gx) {
      const int sourceByte = gy * (kGbWidth / 4) + (gx >> 2);
      const uint8_t shade = (frame[sourceByte] >> ((gx & 3) << 1)) & 0x03;
      const uint8_t intensity = kShadeIntensity[shade];
      const int logicalBaseX = gx * kScale;
      const int logicalBaseY = gy * kScale;

      for (int dy = 0; dy < kScale; ++dy) {
        const int nativeX = logicalBaseY + dy;
        const int bayerRow = (logicalBaseY + dy) & 3;
        for (int dx = 0; dx < kScale; ++dx) {
          const int nativeY = kGameWidth - 1 - (logicalBaseX + dx);
          const int bayerCol = (logicalBaseX + dx) & 3;
          const size_t byteIndex = static_cast<size_t>(nativeY) * nativeStride + (nativeX >> 3);
          const uint8_t mask = static_cast<uint8_t>(0x80 >> (nativeX & 7));
          if (intensity > kBayer4x4[bayerRow][bayerCol]) {
            target[byteIndex] &= static_cast<uint8_t>(~mask);
          } else {
            target[byteIndex] |= mask;
          }
        }
      }
    }
  }
}

uint32_t GameBoyActivity::hashFrame() const {
  uint32_t hash = 2166136261u;
  for (const uint8_t value : renderFrame_) {
    hash ^= value;
    hash *= 16777619u;
  }
  return hash;
}

uint8_t GameBoyActivity::collectInput() {
  uint8_t input = 0;
  if (mappedInput.isPressed(MappedInputManager::Button::Right)) input |= INPUT_RIGHT;
  if (mappedInput.isPressed(MappedInputManager::Button::Left)) input |= INPUT_LEFT;
  if (mappedInput.isPressed(MappedInputManager::Button::Up)) input |= INPUT_UP;
  if (mappedInput.isPressed(MappedInputManager::Button::Down)) input |= INPUT_DOWN;
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) input |= INPUT_A;
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) input |= INPUT_B;
  if (mappedInput.isPressed(MappedInputManager::Button::PageBack)) input |= INPUT_SELECT;
  if (mappedInput.isPressed(MappedInputManager::Button::PageForward)) input |= INPUT_START;

  if (touchPulseMask_ != 0) {
    const unsigned long now = millis();
    if (static_cast<int32_t>(touchPulseUntil_ - now) > 0) {
      input |= touchPulseMask_;
    } else {
      touchPulseMask_ = 0;
      touchPulseUntil_ = 0;
    }
  }

  if (input != 0) inputSinceSave_ = true;
  return input;
}

void GameBoyActivity::captureEmulatorFrame() {
  if (!emulator_ || !emulator_->getFramebuffer()) return;
  const uint8_t* frame = emulator_->getFramebuffer();

#ifndef SIMULATOR
  portENTER_CRITICAL(&frameMux_);
#endif
  memcpy(publishedFrame_.data(), frame, kFrameBytes);
  publishedFrameSequence_.fetch_add(1, std::memory_order_release);
#ifndef SIMULATOR
  portEXIT_CRITICAL(&frameMux_);
#endif
}

bool GameBoyActivity::snapshotLatestFrame() {
  if (publishedFrameSequence_.load(std::memory_order_acquire) == consumedFrameSequence_) return false;

#ifndef SIMULATOR
  portENTER_CRITICAL(&frameMux_);
#endif
  memcpy(renderFrame_.data(), publishedFrame_.data(), kFrameBytes);
  consumedFrameSequence_ = publishedFrameSequence_.load(std::memory_order_relaxed);
#ifndef SIMULATOR
  portEXIT_CRITICAL(&frameMux_);
#endif
  return true;
}

bool GameBoyActivity::startEmulatorTask() {
#ifdef SIMULATOR
  return false;
#else
  if (!emulator_ || !emulatorMutex_) return false;
  if (emulatorTaskHandle_.load(std::memory_order_acquire) != nullptr) return true;

  stopEmulatorTask_.store(false, std::memory_order_release);
  TaskHandle_t handle = nullptr;
  const BaseType_t created =
      xTaskCreatePinnedToCore(emulatorTaskTrampoline, "gb-emulator", 8192, this, 2, &handle, 0);
  if (created != pdPASS || handle == nullptr) {
    Serial.println("[GB] Failed to create emulator task");
    return false;
  }
  emulatorTaskHandle_.store(handle, std::memory_order_release);
  return true;
#endif
}

void GameBoyActivity::stopEmulatorTask() {
#ifndef SIMULATOR
  TaskHandle_t handle = emulatorTaskHandle_.load(std::memory_order_acquire);
  if (!handle) return;

  stopEmulatorTask_.store(true, std::memory_order_release);
  while (emulatorTaskHandle_.load(std::memory_order_acquire) != nullptr) {
    vTaskDelay(1);
  }
#endif
}

#ifndef SIMULATOR
void GameBoyActivity::emulatorTaskTrampoline(void* arg) {
  static_cast<GameBoyActivity*>(arg)->emulatorTaskLoop();
}

void GameBoyActivity::emulatorTaskLoop() {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t framePeriod = std::max<TickType_t>(1, pdMS_TO_TICKS(17));

  while (!stopEmulatorTask_.load(std::memory_order_acquire)) {
    if (xSemaphoreTake(emulatorMutex_, portMAX_DELAY) == pdTRUE) {
      if (emulator_) {
        emulator_->setInput(currentInput_.load(std::memory_order_relaxed));
        emulator_->runFrames(1);
        captureEmulatorFrame();
      }
      xSemaphoreGive(emulatorMutex_);
    }

    vTaskDelayUntil(&lastWake, framePeriod);
  }

  emulatorTaskHandle_.store(nullptr, std::memory_order_release);
  vTaskDelete(nullptr);
}
#endif

void GameBoyActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.isPressed(MappedInputManager::Button::Back)) {
    if (exitChordStartedAt_ == 0) exitChordStartedAt_ = millis();
    if (millis() - exitChordStartedAt_ >= kExitHoldMs) {
      requestClose();
      return;
    }
  } else {
    exitChordStartedAt_ = 0;
  }

  if (!ready_ || !emulator_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) requestClose();
    return;
  }

  currentInput_.store(collectInput(), std::memory_order_relaxed);

  const unsigned long now = millis();

  // Simulator and task-allocation fallback: keep emulation frame-paced instead
  // of returning to the old runFrames(8) burst behavior.
  if (!backgroundEmulation_ && now - lastSynchronousFrameAt_ >= 17) {
    emulator_->setInput(currentInput_.load(std::memory_order_relaxed));
    emulator_->runFrames(1);
    captureEmulatorFrame();
    lastSynchronousFrameAt_ = now;
  }

  // The Game Boy keeps running near 60 Hz independently. The e-ink panel only
  // consumes the newest completed frame at a controlled rate.
  if (now - lastDisplayAt_ >= kDisplayIntervalMs && snapshotLatestFrame()) {
    const uint32_t hash = hashFrame();
    if (hash != lastFrameHash_) {
      lastFrameHash_ = hash;

      const uint32_t renderStartedAt = micros();
      renderGameFrame();
      const uint32_t renderFinishedAt = micros();

      ++displayedFrames_;
      const HalDisplay::RefreshMode mode = (displayedFrames_ % 64u == 0u) ? HalDisplay::STRONG_FAST_REFRESH
                                                                         : HalDisplay::FAST_REFRESH;
      renderer.displayBuffer(mode);
      const uint32_t displayFinishedAt = micros();

      // Set the interval after the blocking e-ink update. This intentionally
      // leaves time for the main input loop between refreshes.
      lastDisplayAt_ = millis();

      if ((displayedFrames_ % 32u) == 0u) {
        Serial.printf("[GB PERF] render=%lu ms display=%lu ms shown=%lu\n",
                      static_cast<unsigned long>((renderFinishedAt - renderStartedAt) / 1000u),
                      static_cast<unsigned long>((displayFinishedAt - renderFinishedAt) / 1000u),
                      static_cast<unsigned long>(displayedFrames_));
      }
    } else {
      lastDisplayAt_ = now;
    }
  }

  if (inputSinceSave_ && now - lastAutoSaveAt_ >= kAutoSaveMs) {
    bool saved = false;
#ifndef SIMULATOR
    if (backgroundEmulation_ && emulatorMutex_) {
      if (xSemaphoreTake(emulatorMutex_, portMAX_DELAY) == pdTRUE) {
        saved = saveSram();
        xSemaphoreGive(emulatorMutex_);
      }
    } else {
      saved = saveSram();
    }
#else
    saved = saveSram();
#endif
    if (saved) inputSinceSave_ = false;
    lastAutoSaveAt_ = millis();
  }
}

bool GameBoyActivity::handleTouchTap(const int x, const int y) {
  if (inRect(x, y, kExitX, kExitY, kExitW, kExitH)) {
    requestClose();
    return true;
  }
  if (!ready_) return true;

  uint8_t pulse = 0;
  if (inRect(x, y, kDpadCenterX, kDpadUpY, kControlSize, kControlSize)) pulse = INPUT_UP;
  if (inRect(x, y, kDpadLeftX, kDpadMiddleY, kControlSize, kControlSize)) pulse = INPUT_LEFT;
  if (inRect(x, y, kDpadRightX, kDpadMiddleY, kControlSize, kControlSize)) pulse = INPUT_RIGHT;
  if (inRect(x, y, kDpadCenterX, kDpadDownY, kControlSize, kControlSize)) pulse = INPUT_DOWN;
  if (inRect(x, y, kButtonBX, kButtonBY, kActionW, kActionH)) pulse = INPUT_B;
  if (inRect(x, y, kButtonAX, kButtonAY, kActionW, kActionH)) pulse = INPUT_A;
  if (inRect(x, y, kSelectX, kMetaY, kMetaW, kMetaH)) pulse = INPUT_SELECT;
  if (inRect(x, y, kStartX, kMetaY, kMetaW, kMetaH)) pulse = INPUT_START;

  if (pulse != 0) {
    touchPulseMask_ = pulse;
    touchPulseUntil_ = millis() + kTouchPulseMs;
    inputSinceSave_ = true;

    // Make the tap visible to the emulator task immediately; the next main-loop
    // collectInput() will merge the physical-button state back in.
    currentInput_.fetch_or(pulse, std::memory_order_relaxed);
    return true;
  }
  return y >= kControlsTop;
}

void GameBoyActivity::requestClose() {
  if (onClose_) onClose_();
}

bool GameBoyActivity::loadSram() {
  if (!emulator_ || !emulator_->sramData) return false;

  std::string path = savePath_;
  const std::string bak = savePath_ + ".bak";
  if (!SdMan.exists(path.c_str()) && SdMan.exists(bak.c_str())) {
    SdMan.rename(bak.c_str(), path.c_str());
  }
  if (!SdMan.exists(path.c_str())) return false;

  FsFile file = SdMan.open(path.c_str(), O_RDONLY);
  if (!file) return false;
  if (file.fileSize() < GBEmulator::GB_SRAM_SIZE) {
    file.close();
    return false;
  }

  if (file.read(emulator_->sramData, GBEmulator::GB_SRAM_SIZE) != static_cast<int>(GBEmulator::GB_SRAM_SIZE)) {
    file.close();
    return false;
  }
  if (file.available() >= 10) {
    uint8_t rtc[10] = {};
    if (file.read(rtc, sizeof(rtc)) == static_cast<int>(sizeof(rtc))) {
      emulator_->rtcSec_ = rtc[0];
      emulator_->rtcMin_ = rtc[1];
      emulator_->rtcHour_ = rtc[2];
      emulator_->rtcDay_ = rtc[3];
      emulator_->rtcDayHi_ = rtc[4];
      memcpy(emulator_->rtcLatched_, rtc + 5, 5);
    }
  }
  file.close();
  Serial.printf("[GB] SRAM loaded: %s\n", path.c_str());
  return true;
}

bool GameBoyActivity::saveSram() {
  if (!emulator_ || !emulator_->sramData) return false;
  SdMan.ensureDirectoryExists("/games/saves");

  const std::string tmp = savePath_ + ".tmp";
  const std::string bak = savePath_ + ".bak";
  if (SdMan.exists(tmp.c_str())) SdMan.remove(tmp.c_str());

  FsFile file = SdMan.open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  if (file.write(emulator_->sramData, GBEmulator::GB_SRAM_SIZE) != GBEmulator::GB_SRAM_SIZE) {
    file.close();
    SdMan.remove(tmp.c_str());
    return false;
  }
  const uint8_t rtc[10] = {emulator_->rtcSec_,        emulator_->rtcMin_,        emulator_->rtcHour_,
                           emulator_->rtcDay_,        emulator_->rtcDayHi_,      emulator_->rtcLatched_[0],
                           emulator_->rtcLatched_[1], emulator_->rtcLatched_[2], emulator_->rtcLatched_[3],
                           emulator_->rtcLatched_[4]};
  file.write(rtc, sizeof(rtc));
  file.sync();
  file.close();

  if (SdMan.exists(bak.c_str())) SdMan.remove(bak.c_str());
  const bool hadCanonical = SdMan.exists(savePath_.c_str());
  if (hadCanonical && !SdMan.rename(savePath_.c_str(), bak.c_str())) {
    SdMan.remove(tmp.c_str());
    return false;
  }
  if (!SdMan.rename(tmp.c_str(), savePath_.c_str())) {
    if (hadCanonical) SdMan.rename(bak.c_str(), savePath_.c_str());
    SdMan.remove(tmp.c_str());
    return false;
  }
  if (hadCanonical) SdMan.remove(bak.c_str());
  Serial.printf("[GB] SRAM saved: %s\n", savePath_.c_str());
  return true;
}

GameBoyBrowserActivity::GameBoyBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::function<void()> onBack)
    : ActivityWithSubactivity("GameBoyBrowser", renderer, mappedInput), onBack_(std::move(onBack)) {}

bool GameBoyBrowserActivity::isGameBoyRom(const std::string& name) {
  return StringUtils::checkFileExtension(name, ".gb") || StringUtils::checkFileExtension(name, ".gbc");
}

void GameBoyBrowserActivity::loadRoms() {
  SdMan.ensureDirectoryExists("/games");
  SdMan.ensureDirectoryExists(kRomDir);
  SdMan.ensureDirectoryExists("/games/saves");

  roms_.clear();
  const auto files = SdMan.listFiles(kRomDir, kMaxRoms);
  roms_.reserve(files.size());
  for (const auto& file : files) {
    const std::string name(file.c_str());
    if (isGameBoyRom(name)) roms_.push_back(name);
  }
  std::sort(roms_.begin(), roms_.end(), [](const std::string& a, const std::string& b) {
    std::string al = a;
    std::string bl = b;
    std::transform(al.begin(), al.end(), al.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(bl.begin(), bl.end(), bl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return al < bl;
  });
  if (selectedIndex_ >= static_cast<int>(roms_.size())) {
    selectedIndex_ = std::max(0, static_cast<int>(roms_.size()) - 1);
  }
  scrollOffset_ = 0;
}

void GameBoyBrowserActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  renderer.setOrientation(GfxRenderer::Portrait);
  loadRoms();
  render();
}

int GameBoyBrowserActivity::visibleRows() const {
  const int usable = renderer.getScreenHeight() - 145;
  return std::max(1, usable / kRowHeight);
}

void GameBoyBrowserActivity::render() {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int top = INX_THEME.drawPageHeader(renderer, "Game Boy ROMs");

  if (roms_.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, top + 80, "No .gb or .gbc ROMs found", true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, top + 116, "Copy ROMs to /games/gb/", true);
  } else {
    const int rows = visibleRows();
    for (int row = 0; row < rows; ++row) {
      const int index = scrollOffset_ + row;
      if (index >= static_cast<int>(roms_.size())) break;
      const int y = top + row * kRowHeight;
      const bool selected = index == selectedIndex_;
      if (selected) renderer.rectangle.fill(0, y, width, kRowHeight, true);
      const std::string label = truncatedLabel(roms_[index], 38);
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 18, textY, label.c_str(), !selected);
      renderer.line.render(0, y + kRowHeight - 1, width, y + kRowHeight - 1, true, LineRender::Style::Dotted);
    }
  }

  const auto labels = mappedInput.mapLabels("Back", roms_.empty() ? "" : "Play", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void GameBoyBrowserActivity::moveSelection(const int delta) {
  if (roms_.empty()) return;
  const int count = static_cast<int>(roms_.size());
  selectedIndex_ = (selectedIndex_ + delta + count) % count;
  const int rows = visibleRows();
  if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
  if (selectedIndex_ >= scrollOffset_ + rows) scrollOffset_ = selectedIndex_ - rows + 1;
  render();
}

void GameBoyBrowserActivity::launchSelected() {
  if (roms_.empty() || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(roms_.size())) return;
  const std::string path = std::string(kRomDir) + "/" + roms_[selectedIndex_];
  enterNewActivity(new GameBoyActivity(renderer, mappedInput, path, [this] {
    exitActivity();
    loadRoms();
    render();
  }));
}

void GameBoyBrowserActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack_) onBack_();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    launchSelected();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    moveSelection(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    moveSelection(1);
  }
}

bool GameBoyBrowserActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) return subActivity->handleTouchTap(x, y);
  if (roms_.empty()) return true;

  const int top = INX_THEME.drawerPageHeaderHeight();
  if (y < top) return false;
  const int row = (y - top) / kRowHeight;
  const int index = scrollOffset_ + row;
  if (row >= 0 && row < visibleRows() && index >= 0 && index < static_cast<int>(roms_.size())) {
    selectedIndex_ = index;
    launchSelected();
    return true;
  }
  return true;
}
