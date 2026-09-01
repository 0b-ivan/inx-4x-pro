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
constexpr int kSourceCardW = 320;
constexpr int kSourceCardH = 533;

ImageRender::Options tarotImageOptions() {
  ImageRender::Options options;
  options.mode = ImageRenderMode::OneBit;
  options.cropToFill = false;

  // Never reuse old Tarot raster cache entries while the deck orientation is
  // being corrected. The source BMPs are vertically inverted for Inx's normal
  // logical coordinate system, so flip only the vertical axis here.
  options.useDisplayCache = false;
  options.flipHorizontal = false;
  options.flipVertical = true;
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

void drawDiamond(GfxRenderer& renderer, const int cx, const int cy, const int radius) {
  renderer.line.render(cx, cy - radius, cx + radius, cy, true);
  renderer.line.render(cx + radius, cy, cx, cy + radius, true);
  renderer.line.render(cx, cy + radius, cx - radius, cy, true);
  renderer.line.render(cx - radius, cy, cx, cy - radius, true);
}

void drawTarotCardBack(GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  renderer.rectangle.render(x, y, w, h, true, true);
  renderer.rectangle.render(x + 7, y + 7, w - 14, h - 14, true, true);
  renderer.rectangle.render(x + 17, y + 17, w - 34, h - 34, true, true);

  const int cx = x + w / 2;
  const int cy = y + h / 2;
  const int outer = std::max(28, std::min(w, h) / 7);
  drawDiamond(renderer, cx, cy, outer);
  drawDiamond(renderer, cx, cy, std::max(16, outer / 2));
  drawTarotStar(renderer, cx, cy, std::max(8, outer / 4));

  const int rayGap = outer + 16;
  renderer.line.render(cx, cy - rayGap - 18, cx, cy - rayGap, true);
  renderer.line.render(cx, cy + rayGap, cx, cy + rayGap + 18, true);
  renderer.line.render(cx - rayGap - 18, cy, cx - rayGap, cy, true);
  renderer.line.render(cx + rayGap, cy, cx + rayGap + 18, cy, true);
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

  constexpr int titleY = 58;
  renderer.text.centered(LITERATA_18_FONT_ID, titleY, "Tarot", true, EpdFontFamily::BOLD);
  drawTarotDivider(renderer, titleY + 47);

  constexpr int stackOffset = 9;
  const int deckY = std::max(148, h / 5 - 12);
  int cardW = std::min(238, std::max(205, w * 50 / 100));
  int cardH = cardW * kSourceCardH / kSourceCardW;
  const int maxCardH = std::max(320, h - deckY - 210);
  if (cardH > maxCardH) {
    cardH = maxCardH;
    cardW = cardH * kSourceCardW / kSourceCardH;
  }
  const int deckX = (w - cardW - stackOffset * 2) / 2;

  renderer.rectangle.render(deckX + stackOffset * 2, deckY + stackOffset * 2, cardW, cardH, true, true);
  renderer.rectangle.render(deckX + stackOffset, deckY + stackOffset, cardW, cardH, true, true);
  drawTarotCardBack(renderer, deckX, deckY, cardW, cardH);

  const int labelY = deckY + cardH + stackOffset * 2 + 30;
  renderer.text.centered(LITERATA_12_FONT_ID, labelY, uiTr("78 cards"), true, EpdFontFamily::BOLD);
  drawTarotDivider(renderer, labelY + 40);
  renderer.text.centered(LITERATA_10_FONT_ID, labelY + 67, uiTr("Tap to draw a card"), true);
}

void TarotActivity::renderCard() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  constexpr int topMargin = 16;
  constexpr int sideMargin = 18;
  constexpr int bottomAreaH = 16;
  const int availableW = std::max(1, w - sideMargin * 2);
  const int availableH = std::max(1, h - topMargin - bottomAreaH);
  int cardW = availableW;
  int cardH = cardW * kSourceCardH / kSourceCardW;
  if (cardH > availableH) {
    cardH = availableH;
    cardW = cardH * kSourceCardW / kSourceCardH;
  }
  const int x = (w - cardW) / 2;
  const int y = topMargin;

  ImageRender::create(renderer, TarotAssets::cardPath(card_)).render(x, y, cardW, cardH, tarotImageOptions());
  renderer.rectangle.render(x - 1, y - 1, cardW + 2, cardH + 2, true);

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
  renderer.clearScreen();
  if (!TarotAssets::installed()) {
    renderer.text.centered(LITERATA_18_FONT_ID, 72, "Tarot", true, EpdFontFamily::BOLD);
    drawTarotDivider(renderer, 120);
    renderer.text.centered(LITERATA_12_FONT_ID, renderer.getScreenHeight() / 2 - 20,
                           uiTr("Tarot files missing"), true, EpdFontFamily::BOLD);
    renderer.text.centered(LITERATA_10_FONT_ID, renderer.getScreenHeight() / 2 + 24,
                           uiTr("Press Select or tap to download about 14 MB"), true);
  } else if (view_ == View::Prompt) renderPrompt();
  else if (view_ == View::Card) renderCard();
  else renderHistory();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
