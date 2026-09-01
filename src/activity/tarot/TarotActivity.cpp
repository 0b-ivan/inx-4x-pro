#include "TarotActivity.h"

#include <GfxRenderer.h>
#include <ImageRender.h>

#include <algorithm>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiI18n.h"
#include "system/UiTheme.h"
#include "TarotDownloadActivity.h"
#include "TarotI18n.h"

namespace {
ImageRender::Options tarotImageOptions() {
  ImageRender::Options options;
  options.mode = ImageRenderMode::OneBit;
  options.cropToFill = false;
  options.useDisplayCache = false;
  // Tarot BMP orientation is normalized centrally in ImageRender.
  options.flipHorizontal = false;
  options.flipVertical = false;
  return options;
}

ImageRender::Options tarotMenuImageOptions() {
  ImageRender::Options options;
  options.mode = ImageRenderMode::OneBit;
  options.cropToFill = false;
  options.useDisplayCache = false;
  return options;
}

void drawTarotStar(GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  renderer.line.render(cx - radius, cy, cx + radius, cy, true);
  renderer.line.render(cx, cy - radius, cx, cy + radius, true);
  const int diagonal = std::max(2, radius * 7 / 10);
  renderer.line.render(cx - diagonal, cy - diagonal, cx + diagonal, cy + diagonal, true);
  renderer.line.render(cx + diagonal, cy - diagonal, cx - diagonal, cy + diagonal, true);
}

void drawTarotDivider(GfxRenderer& renderer, const int y) {
  const int cx = renderer.getScreenWidth() / 2;
  constexpr int halfWidth = 74;
  constexpr int starGap = 13;
  renderer.line.render(cx - halfWidth, y, cx - starGap, y, true);
  renderer.line.render(cx + starGap, y, cx + halfWidth, y, true);
  drawTarotStar(renderer, cx, y, 5);
}
}  // namespace

void TarotActivity::onEnter() {
  deck_.shuffle();
  assets_.load();
  view_ = View::Prompt;
  render();
}

void TarotActivity::drawNext() {
  card_ = deck_.drawNext();
  showMeaning_ = false;
  view_ = View::Card;
  render();
}

void TarotActivity::goBack() {
  if (view_ == View::History) {
    view_ = View::Card;
    render();
  } else if (showMeaning_) {
    showMeaning_ = false;
    render();
  } else {
    onBack_();
  }
}

void TarotActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }

  // Tarot touch contract:
  //   tap        -> draw the next card
  //   long press -> reveal the current card meaning
  // wasScreenLongPress() suppresses the contact, so the release cannot also
  // become a tap and accidentally draw another card.
  if (view_ == View::Card) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenLongPress(touchX, touchY)) {
      showMeaning_ = true;
      render();
      return;
    }
  }

  if (!TarotAssets::installed() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    enterNewActivity(new TarotDownloadActivity(renderer, mappedInput, [this](const bool installed) {
      exitActivity();
      if (installed) assets_.load();
      render();
    }));
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (view_ == View::History) {
      const int pages = std::max(1, (static_cast<int>(deck_.history().size()) + 11) / 12);
      historyPage_ = std::min(historyPage_ + 1, pages - 1);
      render();
    } else {
      drawNext();
    }
    return;
  }
  if (view_ == View::Card && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    showMeaning_ = !showMeaning_;
    render();
    return;
  }
  if (view_ == View::Card && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    view_ = View::History;
    historyPage_ = 0;
    render();
    return;
  }
  if (view_ == View::History && mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    historyPage_ = std::max(0, historyPage_ - 1);
    render();
  }
}

bool TarotActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) return subActivity->handleTouchTap(x, y);
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  if (x < 0 || x >= w || y < 0 || y >= h) return false;
  if (view_ == View::Prompt) {
    if (!TarotAssets::installed()) {
      enterNewActivity(new TarotDownloadActivity(renderer, mappedInput, [this](const bool installed) {
        exitActivity();
        if (installed) assets_.load();
        render();
      }));
      return true;
    }
    drawNext();
    return true;
  }
  if (view_ == View::History) {
    const int pages = std::max(1, (static_cast<int>(deck_.history().size()) + 11) / 12);
    if (x < w / 3) {
      historyPage_ = std::max(0, historyPage_ - 1);
      render();
    } else if (x > w * 2 / 3) {
      historyPage_ = std::min(historyPage_ + 1, pages - 1);
      render();
    } else {
      goBack();
    }
    return true;
  }

  // When the meaning overlay is open, the first tap only dismisses it. A
  // second, separate tap is required to draw the next card.
  if (showMeaning_) {
    showMeaning_ = false;
    render();
    return true;
  }

  drawNext();
  return true;
}

void TarotActivity::renderWrapped(const std::string& text, const int x, int y, const int width, const int maxLines) {
  std::string line;
  size_t pos = 0;
  for (int n = 0; n < maxLines && pos < text.size(); ++n) {
    line.clear();
    while (pos < text.size()) {
      const size_t end = text.find(' ', pos);
      const std::string word = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && renderer.text.getWidth(LITERATA_10_FONT_ID, candidate.c_str()) > width) break;
      line = candidate;
      pos = end == std::string::npos ? text.size() : end + 1;
    }
    renderer.text.render(LITERATA_10_FONT_ID, x, y, line.c_str(), true);
    y += renderer.text.getLineHeight(LITERATA_10_FONT_ID) + 5;
  }
}

void TarotActivity::renderPrompt() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  // menu.png already contains the complete card-stack artwork and both
  // ornamental dividers. Do not draw the old vector fallback on top of it:
  // the PNG decoder may have produced visible pixels even when it reports a
  // decode failure, which previously caused both layouts to be superimposed.
  ImageRender::create(renderer, "/tarot/menu.png").render(0, 0, w, h, tarotMenuImageOptions());

  // Only the localized text is rendered by firmware. Keep the title above the
  // upper divider and the labels in the reserved white bands of the artwork.
  renderer.text.centered(LITERATA_18_FONT_ID, 20, "Tarot", true, EpdFontFamily::BOLD);
  renderer.text.centered(LITERATA_12_FONT_ID, 676, uiTr("78 cards"), true, EpdFontFamily::BOLD);
  renderer.text.centered(LITERATA_10_FONT_ID, 758, uiTr("Tap to draw a card"), true);
}

void TarotActivity::renderCard() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();

  // Rider-Waite cards are 320x533; the X4 Pro is 480x800. The aspect ratios
  // are effectively identical, so render edge-to-edge without UI chrome.
  ImageRender::create(renderer, TarotAssets::cardPath(card_)).render(0, 0, w, h, tarotImageOptions());

  const TarotMeaning m = assets_.meaning(card_);
  if (showMeaning_) {
    const int boxX = 24;
    const int boxW = w - 48;
    const int boxH = std::min(310, h - 120);
    const int boxY = (h - boxH) / 2;
    renderer.rectangle.fill(boxX, boxY, boxW, boxH, false, true);
    renderer.rectangle.render(boxX, boxY, boxW, boxH, true, true);
    renderer.rectangle.render(boxX + 6, boxY + 6, boxW - 12, boxH - 12, true, true);
    renderer.text.centered(LITERATA_12_FONT_ID, boxY + 26, m.name.c_str(), true, EpdFontFamily::BOLD);
    drawTarotDivider(renderer, boxY + 68);
    renderWrapped(tarotMeaningLocalized(card_, m.meaning.c_str()), boxX + 28, boxY + 98, boxW - 56, 7);
  }
}

void TarotActivity::renderHistory() {
  const int w = renderer.getScreenWidth();
  renderer.text.centered(LITERATA_16_FONT_ID, 44, uiTr("Tarot history"), true, EpdFontFamily::BOLD);
  drawTarotDivider(renderer, 88);

  constexpr int cols = 4;
  constexpr int rows = 3;
  constexpr int thumbW = 80;
  constexpr int thumbH = 133;
  const int gapX = (w - cols * thumbW) / (cols + 1);
  const int startY = 112;
  const int start = historyPage_ * cols * rows;
  const auto& history = deck_.history();
  for (int i = 0; i < cols * rows && start + i < static_cast<int>(history.size()); ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int x = gapX + col * (thumbW + gapX);
    const int y = startY + row * (thumbH + 18);
    ImageRender::create(renderer, TarotAssets::thumbPath(history[static_cast<size_t>(start + i)]))
        .render(x, y, thumbW, thumbH, tarotImageOptions());
    renderer.rectangle.render(x, y, thumbW, thumbH, true);
  }
  const int pages = std::max(1, (static_cast<int>(history.size()) + cols * rows - 1) / (cols * rows));
  char page[24];
  std::snprintf(page, sizeof(page), "<   %d / %d   >", historyPage_ + 1, pages);
  renderer.text.centered(LITERATA_10_FONT_ID, renderer.getScreenHeight() - 40, page, true);
}

void TarotActivity::render() {
  renderer.clearScreen(0xFF);
  if (!TarotAssets::installed()) {
    renderer.text.centered(LITERATA_18_FONT_ID, 72, "Tarot", true, EpdFontFamily::BOLD);
    drawTarotDivider(renderer, 120);
    renderer.text.centered(LITERATA_12_FONT_ID, renderer.getScreenHeight() / 2 - 20,
                           uiTr("Tarot files missing"), true, EpdFontFamily::BOLD);
    renderer.text.centered(LITERATA_10_FONT_ID, renderer.getScreenHeight() / 2 + 24,
                           uiTr("Press Select or tap to download about 14 MB"), true);
  } else if (view_ == View::Prompt) {
    renderPrompt();
  } else if (view_ == View::Card) {
    renderCard();
  } else {
    renderHistory();
  }

  // The prompt consists mostly of white paper. A fast E-Ink refresh leaves the
  // previous vector menu visible as ghosting in those areas, which looked like
  // the old UI was still being drawn. Use a full refresh for the menu only;
  // card-to-card interaction stays fast.
  const HalDisplay::RefreshMode refreshMode =
      TarotAssets::installed() && view_ == View::Prompt ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayBuffer(refreshMode);
}
