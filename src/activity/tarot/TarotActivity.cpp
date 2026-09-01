#include "TarotActivity.h"

#include <GfxRenderer.h>
#include <ImageRender.h>

#include <algorithm>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiI18n.h"
#include "system/UiTheme.h"
#include "TarotDownloadActivity.h"

namespace {
constexpr int kFooterH = 44;
constexpr int kSourceCardW = 320;
constexpr int kSourceCardH = 533;
ImageRender::Options tarotImageOptions() {
  ImageRender::Options options;
  options.mode = ImageRenderMode::OneBit;
  options.cropToFill = false;
  options.useDisplayCache = true;
  options.flipHorizontal = true;
  return options;
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
    if (y >= h - kFooterH) {
      if (x < w / 3) goBack();
      else if (x > w * 2 / 3) {
        const int pages = std::max(1, (static_cast<int>(deck_.history().size()) + 11) / 12);
        historyPage_ = (historyPage_ + 1) % pages;
        render();
      }
      return true;
    }
    return false;
  }
  if (y >= h - kFooterH) {
    if (x < w / 4) goBack();
    else if (x < w / 2) {
      view_ = View::History;
      historyPage_ = 0;
      render();
    } else if (x < w * 3 / 4) {
      showMeaning_ = !showMeaning_;
      render();
    } else drawNext();
    return true;
  }
  showMeaning_ = !showMeaning_;
  render();
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
      if (!line.empty() && renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, candidate.c_str()) > width) break;
      line = candidate;
      pos = end == std::string::npos ? text.size() : end + 1;
    }
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, x, y, line.c_str(), true);
    y += renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 5;
  }
}

void TarotActivity::renderPrompt() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_16_FONT_ID, 105, "Tarot", true, EpdFontFamily::BOLD);
  renderer.rectangle.render(w / 2 - 95, 190, 190, 318, true, true);
  renderer.rectangle.render(w / 2 - 84, 201, 168, 296, true, true);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 320, "78", true, EpdFontFamily::BOLD);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 365, uiTr("Tap to draw a card"), true);
  const auto labels = mappedInput.mapLabels(uiTr("Back"), "", "", uiTr("Draw"));
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TarotActivity::renderCard() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  constexpr int topMargin = 8;
  constexpr int sideMargin = 8;
  constexpr int titleAreaH = 38;
  const int availableW = std::max(1, w - sideMargin * 2);
  const int availableH = std::max(1, h - kFooterH - topMargin - titleAreaH);
  int cardW = availableW;
  int cardH = cardW * kSourceCardH / kSourceCardW;
  if (cardH > availableH) {
    cardH = availableH;
    cardW = cardH * kSourceCardW / kSourceCardH;
  }
  const int x = (w - cardW) / 2;
  const int y = topMargin;
  ImageRender::create(renderer, TarotAssets::cardPath(card_)).render(x, y, cardW, cardH, tarotImageOptions());
  renderer.rectangle.render(x - 2, y - 2, cardW + 4, cardH + 4, true);
  const TarotMeaning m = assets_.meaning(card_);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, y + cardH + 10, m.name.c_str(), true,
                         EpdFontFamily::BOLD);
  if (showMeaning_) {
    const int boxX = 28;
    const int boxY = 220;
    const int boxW = w - 56;
    const int boxH = 230;
    renderer.rectangle.fill(boxX, boxY, boxW, boxH, false, true);
    renderer.rectangle.render(boxX, boxY, boxW, boxH, true, true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, boxY + 22, m.name.c_str(), true,
                           EpdFontFamily::BOLD);
    renderer.line.render(boxX + 22, boxY + 58, boxX + boxW - 22, boxY + 58, true);
    renderWrapped(m.meaning, boxX + 24, boxY + 82, boxW - 48, 5);
  }
  const auto labels = mappedInput.mapLabels(uiTr("Back"), uiTr("History"), uiTr("Meaning"), uiTr("Draw"));
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TarotActivity::renderHistory() {
  const int w = renderer.getScreenWidth();
  const int headerBottom = INX_THEME.drawPageHeader(renderer, uiTr("Tarot history"));
  constexpr int cols = 4;
  constexpr int rows = 3;
  constexpr int thumbW = 80;
  constexpr int thumbH = 133;
  const int gapX = (w - cols * thumbW) / (cols + 1);
  const int start = historyPage_ * cols * rows;
  const auto& history = deck_.history();
  for (int i = 0; i < cols * rows && start + i < static_cast<int>(history.size()); ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int x = gapX + col * (thumbW + gapX);
    const int y = headerBottom + 18 + row * (thumbH + 18);
    ImageRender::create(renderer, TarotAssets::thumbPath(history[static_cast<size_t>(start + i)]))
        .render(x, y, thumbW, thumbH, tarotImageOptions());
    renderer.rectangle.render(x, y, thumbW, thumbH, true);
  }
  const int pages = std::max(1, (static_cast<int>(history.size()) + cols * rows - 1) / (cols * rows));
  char page[24];
  std::snprintf(page, sizeof(page), "%d / %d", historyPage_ + 1, pages);
  renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, renderer.getScreenHeight() - 72, page, true);
  const auto labels = mappedInput.mapLabels(uiTr("Back"), "", uiTr("Prev"), uiTr("Next"));
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TarotActivity::render() {
  renderer.clearScreen();
  if (!TarotAssets::installed()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, renderer.getScreenHeight() / 2 - 20,
                           uiTr("Tarot files missing"), true, EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, renderer.getScreenHeight() / 2 + 20,
                           uiTr("Press Select or tap to download about 14 MB"), true);
    const auto labels = mappedInput.mapLabels(uiTr("Back"), uiTr("Download"), "", "");
    renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (view_ == View::Prompt) renderPrompt();
  else if (view_ == View::Card) renderCard();
  else renderHistory();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
