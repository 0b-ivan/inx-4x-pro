#include "CountdownActivity.h"
#include "../Shelf.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "../../components/UITheme.h"
#include "fontIds.h"

static constexpr int SEG_W = 52;
static constexpr int SEG_H = 8;
static constexpr int HALF_H = SEG_W / 2;

static constexpr int DIGIT_W = SEG_W + 2 * SEG_H;
static constexpr int DIGIT_H = 2 * HALF_H + 3 * SEG_H;

static constexpr int COLON_W = 16;
static constexpr int DIGIT_GAP = 4;
static constexpr int TIME_TOTAL_W = 3 * (2 * DIGIT_W + DIGIT_GAP) + 2 * COLON_W;

static constexpr uint8_t SEGS[10] = {
    0x01 | 0x02 | 0x04 | 0x10 | 0x20 | 0x40,
    0x04 | 0x20,
    0x01 | 0x04 | 0x08 | 0x10 | 0x40,
    0x01 | 0x04 | 0x08 | 0x20 | 0x40,
    0x02 | 0x04 | 0x08 | 0x20,
    0x01 | 0x02 | 0x08 | 0x20 | 0x40,
    0x01 | 0x02 | 0x08 | 0x10 | 0x20 | 0x40,
    0x01 | 0x04 | 0x20,
    0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20 | 0x40,
    0x01 | 0x02 | 0x04 | 0x08 | 0x20 | 0x40,
};

void CountdownActivity::drawLargeDigit(int x, int y, int digit, int segW, int segH, bool invert) const {
  if (digit < 0 || digit > 9) return;
  const uint8_t pat = SEGS[digit];

  const int halfH = segW / 2;
  const int dw = segW + 2 * segH;
  const int dh = 2 * halfH + 3 * segH;

  if (invert) {
    renderer.fillRect(x - 2, y - 2, dw + 4, dh + 4, false);
    renderer.drawRect(x - 2, y - 2, dw + 4, dh + 4, true);
  }

  if (pat & 0x01) renderer.fillRect(x + segH, y, segW, segH, true);
  if (pat & 0x08) renderer.fillRect(x + segH, y + segH + halfH, segW, segH, true);
  if (pat & 0x40) renderer.fillRect(x + segH, y + 2 * segH + 2 * halfH, segW, segH, true);
  if (pat & 0x02) renderer.fillRect(x, y + segH, segH, halfH, true);
  if (pat & 0x04) renderer.fillRect(x + segW + segH, y + segH, segH, halfH, true);
  if (pat & 0x10) renderer.fillRect(x, y + 2 * segH + halfH, segH, halfH, true);
  if (pat & 0x20) renderer.fillRect(x + segW + segH, y + 2 * segH + halfH, segH, halfH, true);
}

void CountdownActivity::drawLargeTime(int centerX, int y, int h, int m, int s,
                                       int highlightField) const {
  const int sw = SEG_W;
  const int sh = SEG_H;
  const int dw = DIGIT_W;
  const int dh = DIGIT_H;
  const int gap = DIGIT_GAP;
  const int cw = COLON_W;

  const int totalW = 3 * (2 * dw + gap) + 2 * cw;
  int cx = centerX - totalW / 2;

  const int dotSz = sh + 2;
  const int dot1Y = y + dh / 3 - dotSz / 2;
  const int dot2Y = y + 2 * dh / 3 - dotSz / 2;

  auto drawPair = [&](int px, int val, int field) {
    int tens = val / 10;
    int ones = val % 10;
    bool inv = (highlightField == field);
    drawLargeDigit(px, y, tens, sw, sh, inv);
    drawLargeDigit(px + dw + gap, y, ones, sw, sh, inv);
    if (inv) {
      int underY = y + dh + 4;
      renderer.fillRect(px - 2, underY, 2 * dw + gap + 4, sh / 2 + 1, true);
    }
  };

  drawPair(cx, h, 0);
  cx += 2 * dw + gap;

  renderer.fillRect(cx + cw / 2 - dotSz / 2, dot1Y, dotSz, dotSz, true);
  renderer.fillRect(cx + cw / 2 - dotSz / 2, dot2Y, dotSz, dotSz, true);
  cx += cw;

  drawPair(cx, m, 1);
  cx += 2 * dw + gap;

  renderer.fillRect(cx + cw / 2 - dotSz / 2, dot1Y, dotSz, dotSz, true);
  renderer.fillRect(cx + cw / 2 - dotSz / 2, dot2Y, dotSz, dotSz, true);
  cx += cw;

  drawPair(cx, s, 2);
}

void CountdownActivity::getRemainingHMS(int& h, int& m, int& s) const {
  unsigned long remMs;
  if (state == RUNNING) {
    unsigned long now = millis();
    remMs = (now >= targetTime) ? 0 : (targetTime - now);
  } else {
    remMs = pauseRemaining;
  }
  unsigned long remSec = remMs / 1000;
  h = static_cast<int>(remSec / 3600);
  m = static_cast<int>((remSec % 3600) / 60);
  s = static_cast<int>(remSec % 60);
}

void CountdownActivity::onEnter() {
  Activity::onEnter();
  state = SET_TIME;
  hours = 0;
  minutes = 30;
  seconds = 0;
  editField = 1;
  targetTime = 0;
  pauseRemaining = 0;
  lastDisplayMs = 0;
  requestUpdate();
}

void CountdownActivity::loop() {
  if (state == SET_TIME) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      editField = (editField == 0) ? 2 : editField - 1;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      editField = (editField == 2) ? 0 : editField + 1;
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (editField == 0) { hours = (hours + 1) % 24; }
      else if (editField == 1) { minutes = (minutes + 1) % 60; }
      else { seconds = (seconds + 1) % 60; }
      requestUpdate();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (editField == 0) { hours = (hours == 0) ? 23 : hours - 1; }
      else if (editField == 1) { minutes = (minutes == 0) ? 59 : minutes - 1; }
      else { seconds = (seconds == 0) ? 59 : seconds - 1; }
      requestUpdate();
    }
    {
      int tx_, ty_;
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasScreenTapped(tx_, ty_)) {
        unsigned long totalMs =
            static_cast<unsigned long>(hours) * 3600000UL +
            static_cast<unsigned long>(minutes) * 60000UL +
            static_cast<unsigned long>(seconds) * 1000UL;
        if (totalMs == 0) return;
        origHours = hours;
        origMinutes = minutes;
        origSeconds = seconds;
        targetTime = millis() + totalMs;
        state = RUNNING;
        lastDisplayMs = 0;
        requestUpdate();
      }
    }
    return;
  }

  if (state == RUNNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      unsigned long now = millis();
      pauseRemaining = (now >= targetTime) ? 0 : (targetTime - now);
      state = PAUSED;
      requestUpdate();
      return;
    }
    {
      int tx_, ty_;
      if (mappedInput.wasScreenTapped(tx_, ty_)) {
        unsigned long now = millis();
        pauseRemaining = (now >= targetTime) ? 0 : (targetTime - now);
        state = PAUSED;
        requestUpdate();
        return;
      }
    }
    if (millis() >= targetTime) {
      state = FINISHED;
      requestUpdate();
      return;
    }
    unsigned long now = millis();
    if (now - lastDisplayMs >= DISPLAY_INTERVAL) {
      lastDisplayMs = now;
      requestUpdate();
    }
    return;
  }

  if (state == PAUSED) {
    {
      int tx_, ty_;
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasScreenTapped(tx_, ty_)) {
        targetTime = millis() + pauseRemaining;
        pauseRemaining = 0;
        state = RUNNING;
        lastDisplayMs = 0;
        requestUpdate();
        return;
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = SET_TIME;
      hours = origHours;
      minutes = origMinutes;
      seconds = origSeconds;
      editField = 1;
      requestUpdate();
      return;
    }
    return;
  }

  if (state == FINISHED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = SET_TIME;
      hours = origHours;
      minutes = origMinutes;
      seconds = origSeconds;
      editField = 1;
      requestUpdate();
      return;
    }
    {
      int tx_, ty_;
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
          mappedInput.wasScreenTapped(tx_, ty_)) {
        unsigned long totalMs =
            static_cast<unsigned long>(origHours) * 3600000UL +
            static_cast<unsigned long>(origMinutes) * 60000UL +
            static_cast<unsigned long>(origSeconds) * 1000UL;
        targetTime = millis() + totalMs;
        state = RUNNING;
        lastDisplayMs = 0;
        requestUpdate();
        return;
      }
    }
    return;
  }
}

void CountdownActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Countdown");

  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int contentTop = headerBottom + metrics.verticalSpacing;
  const int hintsTop = pageHeight - metrics.buttonHintsHeight;
  const int contentH = hintsTop - contentTop;
  const int centerX = pageWidth / 2;

  if (state == SET_TIME) {
    renderer.drawCenteredText(SMALL_FONT_ID, contentTop + 8, "Set countdown time");

    const int timeY = contentTop + contentH / 2 - DIGIT_H / 2 - 10;
    drawLargeTime(centerX, timeY, hours, minutes, seconds, editField);

    const int labelsY = timeY + DIGIT_H + 18;
    {
      const int totalW = 3 * (2 * DIGIT_W + DIGIT_GAP) + 2 * COLON_W;
      const int x0 = centerX - totalW / 2;
      const int hhCX = x0 + DIGIT_W + DIGIT_GAP / 2;
      const int mmCX = x0 + 2 * DIGIT_W + DIGIT_GAP + COLON_W + DIGIT_W + DIGIT_GAP / 2;
      const int ssCX = x0 + 2 * (2 * DIGIT_W + DIGIT_GAP + COLON_W) + DIGIT_W + DIGIT_GAP / 2;

      const char* lblH = "HOURS";
      const char* lblM = "MINUTES";
      const char* lblS = "SECONDS";
      int wH = renderer.getTextWidth(SMALL_FONT_ID, lblH);
      int wM = renderer.getTextWidth(SMALL_FONT_ID, lblM);
      int wS = renderer.getTextWidth(SMALL_FONT_ID, lblS);
      renderer.drawText(SMALL_FONT_ID, hhCX - wH / 2, labelsY, lblH);
      renderer.drawText(SMALL_FONT_ID, mmCX - wM / 2, labelsY, lblM);
      renderer.drawText(SMALL_FONT_ID, ssCX - wS / 2, labelsY, lblS);
    }

    const auto labels = mappedInput.mapLabels("Back", "Start", "+", "-");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == RUNNING) {
    int h, m, s;
    getRemainingHMS(h, m, s);

    const int timeY = contentTop + 20;
    drawLargeTime(centerX, timeY, h, m, s, -1);

    unsigned long totalMs =
        static_cast<unsigned long>(origHours) * 3600000UL +
        static_cast<unsigned long>(origMinutes) * 60000UL +
        static_cast<unsigned long>(origSeconds) * 1000UL;

    const int barX = metrics.contentSidePadding;
    const int barW = pageWidth - 2 * metrics.contentSidePadding;
    const int barH = 14;
    const int barY = timeY + DIGIT_H + metrics.verticalSpacing + 10;

    renderer.drawRect(barX, barY, barW, barH, true);
    if (totalMs > 0) {
      unsigned long now = millis();
      unsigned long elapsed = (now >= targetTime) ? totalMs : (totalMs - (targetTime - now));
      if (elapsed > totalMs) elapsed = totalMs;
      int fillW = static_cast<int>((long long)elapsed * (barW - 2) / (long long)totalMs);
      if (fillW > 0)
        renderer.fillRect(barX + 1, barY + 1, fillW, barH - 2, true);
    }

    {
      unsigned long now = millis();
      unsigned long remMs = (now >= targetTime) ? 0 : (targetTime - now);
      unsigned long elMs = (totalMs > remMs) ? (totalMs - remMs) : 0;
      char buf[32];
      int eh = static_cast<int>(elMs / 3600000UL);
      int em = static_cast<int>((elMs % 3600000UL) / 60000UL);
      int es = static_cast<int>((elMs % 60000UL) / 1000UL);
      int th = origHours, tm = origMinutes, ts = origSeconds;
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d / %02d:%02d:%02d", eh, em, es, th, tm, ts);
      renderer.drawCenteredText(SMALL_FONT_ID, barY + barH + 6, buf);
    }

    const auto labels = mappedInput.mapLabels("Pause", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == PAUSED) {
    int h, m, s;
    getRemainingHMS(h, m, s);

    const int timeY = contentTop + 20;
    drawLargeTime(centerX, timeY, h, m, s, -1);

    renderer.drawCenteredText(UI_12_FONT_ID, timeY + DIGIT_H + metrics.verticalSpacing + 6,
                               "PAUSED", true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("Reset", "Resume", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FINISHED) {
    const int midY = contentTop + contentH / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 40, "TIME'S UP", true, EpdFontFamily::BOLD);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", origHours, origMinutes, origSeconds);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, buf);

    renderer.drawLine(metrics.contentSidePadding, midY - 48,
                      pageWidth - metrics.contentSidePadding, midY - 48, true);
    renderer.drawLine(metrics.contentSidePadding, midY + 30,
                      pageWidth - metrics.contentSidePadding, midY + 30, true);

    const auto labels = mappedInput.mapLabels("Reset", "Restart", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.displayBuffer();
}
