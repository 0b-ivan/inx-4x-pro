#pragma once

/**
 * @file MappedInputManager.h
 * @brief Public interface and types for MappedInputManager.
 */

#include <HalGPIO.h>

#include <cstdint>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };
  enum class MotionGesture : uint8_t { None, Previous, Next };
  enum class SwipeDir : uint8_t { None, Left, Right, Up, Down };
  enum class RowTouch : uint8_t { None, Down, Tap };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  /** Labels for the physical page (side) buttons, top then bottom, per Side Button Layout setting. */
  struct SideLabels {
    const char* top;
    const char* bottom;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  /**
   * When true, Up/Down, Left/Right, and PageBack/PageForward are swapped before GPIO lookup.
   * Use with GfxRenderer::LandscapeClockwise (180° vs panel) so physical directions match the
   * rotated framebuffer; clear when leaving that mode or the reader.
   */
  void setInvertDirectionalAxes180(bool invert) { invertDirectionalAxes180_ = invert; }
  bool invertDirectionalAxes180() const { return invertDirectionalAxes180_; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity) const;
  unsigned long getHeldTime() const;

  // CrossPoint-style touch bridge for legacy hand-rendered Inx screens. The
  // returned coordinates are logical screen pixels in the renderer's CURRENT
  // orientation. Activities must hit-test their real rows/buttons/tabs instead
  // of treating arbitrary screen regions as directional button presses.
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  bool isScreenTouchHeld(int& x, int& y) const;
  bool wasScreenLongPress(int& x, int& y) const;
  bool wasScreenTouchReleased() const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;
  SwipeDir wasSwipe() const;
  bool wasHomeGesture() const;
  bool wasMenuGesture() const;

  /** Raw GPIO read (layout + invert still apply to HalGPIO indices). For fixed chords use HalGPIO::BTN_* ). */
  bool rawHalIsPressed(uint8_t halButtonIndex) const;

  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

  /**
   * Like mapLabels, but Left/Right slot text follows Settings → Next & Previous Mapping and drawer
   * orientation (portrait vs landscape list uses different prev/next buttons). Used for TOC lists.
   */
  Labels mapLabelsWithReaderNav(const char* back, const char* confirm, const char* prevSym, const char* nextSym,
                                bool landscapeDrawer) const;

  /** « / » order follows which GPIO is wired as page-back vs page-forward (see Side Button Layout). */
  SideLabels mapSideLabels() const;

 private:
  HalGPIO& gpio;
  bool invertDirectionalAxes180_ = false;
  mutable bool touchHeldOverrideValid_ = false;
  mutable unsigned long touchHeldOverrideMs_ = 0;
  mutable unsigned long touchHeldOverrideAt_ = 0;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool wasBackGesture() const;
  void rememberTouchHeldTime() const;
};
