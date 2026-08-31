#pragma once

#ifndef SIMULATOR

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "images/FrontlightIcons.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/X4ProQuickPrefs.h"

extern HalDisplay display;

/** CrossPoint-style top-edge quick settings sheet for X4 Pro. */
class QuickSettingsDrawer {
 public:
  QuickSettingsDrawer(GfxRenderer& renderer, MappedInputManager& input) : renderer(renderer), input(input) {}

  ~QuickSettingsDrawer() { releaseSnapshot(); }

  bool isOpen() const { return open_; }

  bool open() {
    if (open_ || !input.hasTouch() || !frontlight.present()) return false;

    uint8_t* frame = renderer.getFrameBuffer();
    snapshotSize_ = renderer.getBufferSize();
    if (!frame || snapshotSize_ == 0) return false;

    snapshot_ = static_cast<uint8_t*>(heap_caps_malloc(snapshotSize_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!snapshot_) snapshot_ = static_cast<uint8_t*>(malloc(snapshotSize_));
    if (!snapshot_) {
      Serial.printf("[%lu] [QUICK] framebuffer snapshot allocation failed (%u bytes)\n", millis(),
                    static_cast<unsigned>(snapshotSize_));
      snapshotSize_ = 0;
      return false;
    }

    memcpy(snapshot_, frame, snapshotSize_);
    open_ = true;
    dragging_ = false;
    lastDragRenderAt_ = 0;
    render();
    Serial.printf("[%lu] [QUICK] opened brightness=%u warmth=%u light=%s\n", millis(), frontlight.brightness(),
                  frontlight.warmth(), frontlight.isOn() ? "on" : "off");
    return true;
  }

  bool loop() {
    if (!open_) return false;

    if (input.wasReleased(MappedInputManager::Button::Back) || input.wasHomeGesture()) {
      close(false);
      return true;
    }

    if (input.wasSwipe() == MappedInputManager::SwipeDir::Up) {
      close(false);
      return true;
    }

    int x = 0;
    int y = 0;
    if (input.isScreenTouchHeld(x, y)) {
      if (setSliderFromPoint(x, y)) {
        dragging_ = true;
        const unsigned long now = millis();
        if (now - lastDragRenderAt_ >= 120) {
          lastDragRenderAt_ = now;
          render();
        }
        return true;
      }
    }

    if (input.wasScreenTouchReleased() && dragging_) {
      dragging_ = false;
      render();
      return true;
    }

    if (!input.wasScreenTapped(x, y)) return true;

    if (y >= panelBottom_) {
      close(false);
      return true;
    }

    handleTap(x, y);
    return true;
  }

 private:
  static constexpr int side_ = 16;
  static constexpr int gap_ = 10;
  static constexpr int buttonW_ = 58;
  static constexpr int controlH_ = 58;
  static constexpr int brightnessY_ = 50;
  static constexpr int warmthY_ = 160;
  static constexpr int brightnessSliderX_ = side_ + buttonW_ + gap_;
  static constexpr int brightnessSliderW_ = 244;
  static constexpr int warmthSliderX_ = side_ + buttonW_ + gap_;
  static constexpr int warmthSliderW_ = 312;
  static constexpr int tileTop_ = 258;
  static constexpr int tileGap_ = 14;
  static constexpr int tileH_ = 92;
  static constexpr int panelBottom_ = 490;

  GfxRenderer& renderer;
  MappedInputManager& input;
  uint8_t* snapshot_ = nullptr;
  size_t snapshotSize_ = 0;
  bool open_ = false;
  bool dragging_ = false;
  unsigned long lastDragRenderAt_ = 0;

  static bool inRect(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
  }

  static const char* orientationLabel(const uint8_t orientation) {
    switch (orientation) {
      case SystemSetting::PORTRAIT:
        return "Hochformat";
      case SystemSetting::LANDSCAPE_CW:
        return "Querformat";
      case SystemSetting::INVERTED:
        return "Kopfueber";
      case SystemSetting::LANDSCAPE_CCW:
        return "Quer links";
      default:
        return "Drehen";
    }
  }

  void releaseSnapshot() {
    if (snapshot_) {
      free(snapshot_);
      snapshot_ = nullptr;
    }
    snapshotSize_ = 0;
  }

  void restoreSnapshot() const {
    if (!snapshot_ || snapshotSize_ == 0) return;
    uint8_t* frame = renderer.getFrameBuffer();
    if (frame) memcpy(frame, snapshot_, snapshotSize_);
  }

  void close(const bool fullRefresh) {
    if (!open_) return;
    frontlight.saveSettings();
    restoreSnapshot();
    renderer.displayBuffer(fullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
    releaseSnapshot();
    open_ = false;
    dragging_ = false;
    Serial.printf("[%lu] [QUICK] closed%s\n", millis(), fullRefresh ? " with full refresh" : "");
  }

  void setBrightness(const int value) {
    const uint8_t next = static_cast<uint8_t>(std::max(1, std::min(100, value)));
    frontlight.setBrightness(next);
    if (!frontlight.isOn()) frontlight.setOn(true);
  }

  void setWarmth(const int value) {
    frontlight.setWarmth(static_cast<uint8_t>(std::max(0, std::min(100, value))));
  }

  bool setSliderFromPoint(const int x, const int y) {
    if (inRect(x, y, brightnessSliderX_, brightnessY_, brightnessSliderW_, controlH_)) {
      const int pct = ((x - brightnessSliderX_) * 100 + brightnessSliderW_ / 2) / brightnessSliderW_;
      setBrightness(pct);
      return true;
    }
    if (inRect(x, y, warmthSliderX_, warmthY_, warmthSliderW_, controlH_)) {
      const int pct = ((x - warmthSliderX_) * 100 + warmthSliderW_ / 2) / warmthSliderW_;
      setWarmth(pct);
      return true;
    }
    return false;
  }

  void handleTap(const int x, const int y) {
    const int width = renderer.getScreenWidth();
    const int plusX = width - side_ - buttonW_ - gap_ - buttonW_;
    const int toggleX = width - side_ - buttonW_;
    const int warmPlusX = width - side_ - buttonW_;

    if (inRect(x, y, side_, brightnessY_, buttonW_, controlH_)) {
      setBrightness(static_cast<int>(frontlight.brightness()) - 1);
      render();
      return;
    }
    if (inRect(x, y, brightnessSliderX_, brightnessY_, brightnessSliderW_, controlH_)) {
      setSliderFromPoint(x, y);
      render();
      return;
    }
    if (inRect(x, y, plusX, brightnessY_, buttonW_, controlH_)) {
      setBrightness(static_cast<int>(frontlight.brightness()) + 1);
      render();
      return;
    }
    if (inRect(x, y, toggleX, brightnessY_, buttonW_, controlH_)) {
      frontlight.toggle();
      render();
      return;
    }

    if (inRect(x, y, side_, warmthY_, buttonW_, controlH_)) {
      setWarmth(static_cast<int>(frontlight.warmth()) - 1);
      render();
      return;
    }
    if (inRect(x, y, warmthSliderX_, warmthY_, warmthSliderW_, controlH_)) {
      setSliderFromPoint(x, y);
      render();
      return;
    }
    if (inRect(x, y, warmPlusX, warmthY_, buttonW_, controlH_)) {
      setWarmth(static_cast<int>(frontlight.warmth()) + 1);
      render();
      return;
    }

    const int tileW = (width - 2 * side_ - tileGap_) / 2;
    const int rightX = side_ + tileW + tileGap_;
    const int row2 = tileTop_ + tileH_ + tileGap_;

    // Same four tiles as CrossPoint's X4 Pro frontlight panel.
    if (inRect(x, y, side_, tileTop_, tileW, tileH_)) {
      const bool next = !X4ProQuickPrefs::nightMode();
      X4ProQuickPrefs::setNightMode(next);
      display.setInverted(next);
      render(HalDisplay::FULL_REFRESH);
      return;
    }
    if (inRect(x, y, rightX, tileTop_, tileW, tileH_)) {
      close(true);
      return;
    }
    if (inRect(x, y, side_, row2, tileW, tileH_)) {
      READER_SETTINGS.orientation = static_cast<uint8_t>((READER_SETTINGS.orientation + 1) % SystemSetting::ORIENTATION_COUNT);
      READER_SETTINGS.saveToFile();
      render();
      return;
    }
    if (inRect(x, y, rightX, row2, tileW, tileH_)) {
      X4ProQuickPrefs::setReaderTouchEnabled(!X4ProQuickPrefs::readerTouchEnabled());
      render();
    }
  }

  void drawCentered(const int font, const int x, const int y, const int width, const char* text,
                    const bool black = true, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    const int textW = renderer.text.getWidth(font, text, style);
    renderer.text.render(font, x + (width - textW) / 2, y, text, black, style);
  }

  void drawButton(const int x, const int y, const int width, const int height, const char* text,
                  const bool active = false) const {
    renderer.rectangle.fill(x, y, width, height,
                            static_cast<int>(active ? GfxRenderer::FillTone::Ink : GfxRenderer::FillTone::Paper), true);
    renderer.rectangle.render(x, y, width, height, true, true);
    const int lineH = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    drawCentered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, x, y + (height - lineH) / 2, width, text, !active,
                 EpdFontFamily::BOLD);
  }

  void drawTile(const int x, const int y, const int width, const int height, const char* line1,
                const char* line2 = nullptr, const bool active = false) const {
    renderer.rectangle.fill(x, y, width, height,
                            static_cast<int>(active ? GfxRenderer::FillTone::Ink : GfxRenderer::FillTone::Paper), true);
    renderer.rectangle.render(x, y, width, height, true, true);
    const int lineH = renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
    const int lines = line2 ? 2 : 1;
    const int firstY = y + (height - lines * lineH - (lines - 1) * 5) / 2;
    drawCentered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, x, firstY, width, line1, !active, EpdFontFamily::BOLD);
    if (line2) drawCentered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, x, firstY + lineH + 5, width, line2, !active);
  }

  void drawSlider(const int x, const int y, const int width, const int height, const uint8_t value) const {
    renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Paper), true);
    renderer.rectangle.render(x, y, width, height, true, true);

    const int innerX = x + 3;
    const int innerY = y + 3;
    const int innerW = width - 6;
    const int innerH = height - 6;
    const int fillW = (innerW * static_cast<int>(value)) / 100;
    if (fillW > 0) renderer.rectangle.fill(innerX, innerY, fillW, innerH, static_cast<int>(GfxRenderer::FillTone::Ink), true);

    constexpr int knobW = 42;
    const int travel = std::max(0, innerW - knobW);
    const int knobX = innerX + (travel * static_cast<int>(value)) / 100;
    renderer.rectangle.fill(knobX, innerY, knobW, innerH, static_cast<int>(GfxRenderer::FillTone::Paper), true);
    renderer.rectangle.render(knobX, innerY, knobW, innerH, true, true);
  }

  void drawCaption(const char* label, const uint8_t value, const int y) const {
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(value));
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, side_, y, label, true, EpdFontFamily::BOLD);
    const int pctW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, pct, EpdFontFamily::BOLD);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenWidth() - side_ - pctW, y, pct, true,
                         EpdFontFamily::BOLD);
  }

  void render(const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH) const {
    if (!open_) return;
    restoreSnapshot();

    const int width = renderer.getScreenWidth();
    renderer.rectangle.fill(0, 0, width, panelBottom_, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.line.render(0, panelBottom_ - 1, width - 1, panelBottom_ - 1);

    drawCaption("Brightness", frontlight.brightness(), 18);
    drawButton(side_, brightnessY_, buttonW_, controlH_, "-");
    drawSlider(brightnessSliderX_, brightnessY_, brightnessSliderW_, controlH_, frontlight.brightness());
    const int plusX = width - side_ - buttonW_ - gap_ - buttonW_;
    const int toggleX = width - side_ - buttonW_;
    drawButton(plusX, brightnessY_, buttonW_, controlH_, "+");
    drawButton(toggleX, brightnessY_, buttonW_, controlH_, "", frontlight.isOn());
    const uint8_t* sunIcon = frontlight.isOn() ? FrontlightSunFilled32 : FrontlightSunOutline32;
    renderer.bitmap.icon(sunIcon, toggleX + (buttonW_ - 32) / 2, brightnessY_ + (controlH_ - 32) / 2, 32, 32,
                         BitmapRender::Orientation::None, frontlight.isOn());

    drawCaption("Warmth", frontlight.warmth(), 128);
    drawButton(side_, warmthY_, buttonW_, controlH_, "-");
    drawSlider(warmthSliderX_, warmthY_, warmthSliderW_, controlH_, frontlight.warmth());
    drawButton(width - side_ - buttonW_, warmthY_, buttonW_, controlH_, "+");

    const int tileW = (width - 2 * side_ - tileGap_) / 2;
    const int rightX = side_ + tileW + tileGap_;
    const int row2 = tileTop_ + tileH_ + tileGap_;
    drawTile(side_, tileTop_, tileW, tileH_, "Nachtmodus", X4ProQuickPrefs::nightMode() ? "An" : "Aus",
             X4ProQuickPrefs::nightMode());
    drawTile(rightX, tileTop_, tileW, tileH_, "Bildschirm", "regenerieren");
    drawTile(side_, row2, tileW, tileH_, "Ausrichtung", orientationLabel(READER_SETTINGS.orientation));
    drawTile(rightX, row2, tileW, tileH_, "Reader Touch", X4ProQuickPrefs::readerTouchEnabled() ? "An" : "Aus",
             X4ProQuickPrefs::readerTouchEnabled());

    const int grabberW = 72;
    const int grabberH = 5;
    renderer.rectangle.fill((width - grabberW) / 2, panelBottom_ - 19, grabberW, grabberH,
                            static_cast<int>(GfxRenderer::FillTone::Ink), true);

    renderer.displayBuffer(mode);
  }
};

#endif  // !SIMULATOR
