#include "TarotActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <cstdio>

#include "../../activities/network/WifiSelectionActivity.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "TarotCardRenderer.h"
#include "TarotDownloadActivity.h"
#include "TarotI18n.h"
#include "fontIds.h"

namespace {
// Prose (card meanings, hints, history) carries German umlauts, which the
// Jersey display font cannot draw. UI_10 is the global Noto Sans the rest of
// the firmware sets its UI text in; it covers Latin-1 and reads as the device.
constexpr int kProseFontId = UI_10_FONT_ID;
constexpr int kPanelMargin = 20;
constexpr int kPanelPad = 12;
}  // namespace

// Draws wrapped text on an opaque panel anchored to the bottom of the screen,
// so it stays legible over a card image instead of running off its edges.
void TarotActivity::drawBottomPanel(const char* text, const int maxLines) {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int textWidth = w - 2 * kPanelMargin - 2 * kPanelPad;
  const std::vector<std::string> lines = renderer.wrappedText(kProseFontId, text, textWidth, maxLines);
  if (lines.empty()) return;

  const int lineHeight = renderer.getLineHeight(kProseFontId);
  const int panelHeight = static_cast<int>(lines.size()) * lineHeight + 2 * kPanelPad;
  const int panelY = h - panelHeight - kPanelMargin;
  renderer.fillRect(kPanelMargin, panelY, w - 2 * kPanelMargin, panelHeight, false);
  renderer.drawRect(kPanelMargin, panelY, w - 2 * kPanelMargin, panelHeight, true);

  int y = panelY + kPanelPad;
  for (const std::string& line : lines) {
    renderer.drawCenteredText(kProseFontId, y, line.c_str(), true);
    y += lineHeight;
  }
}

std::unique_ptr<Activity> TarotActivity::create(GfxRenderer& r, MappedInputManager& i) {
  return makeUniqueNoThrow<TarotActivity>(r, i);
}

void TarotActivity::onEnter() {
  toybox::ensureFonts(renderer);
  assets_.load();
  deck_.shuffle();
  card_ = -1;
  showMeaning_ = false;
  history_ = false;
  requestUpdate();
}

void TarotActivity::drawNext() {
  card_ = deck_.drawNext();
  showMeaning_ = false;
  history_ = false;
  requestUpdate();
}

void TarotActivity::startDownload() {
  startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           // The Wi-Fi picker closes on a button press, so its release arrives here.
                           awaitRelease_ = true;
                           if (result.isCancelled) return;
                           startActivityForResult(makeUniqueNoThrow<TarotDownloadActivity>(renderer, mappedInput),
                                                  [this](const ActivityResult&) {
                                                    awaitRelease_ = true;
                                                    assets_.load();
                                                    requestUpdate();
                                                  });
                         });
}

void TarotActivity::loop() {
  if (awaitRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.isPressed(MappedInputManager::Button::Confirm))
      awaitRelease_ = false;
    return;
  }
  // Without the deck the only actions are download or leave; drawing a card
  // would show "Unknown", so every draw input routes to the downloader instead.
  if (!TarotAssets::installed()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    int tx = 0, ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right) || mappedInput.wasScreenTapped(tx, ty))
      startDownload();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (history_ || showMeaning_) {
      history_ = false;
      showMeaning_ = false;
      requestUpdate();
    } else
      shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (card_ >= 0) {
      history_ = true;
      showMeaning_ = false;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (card_ >= 0) showMeaning_ = !showMeaning_;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    drawNext();
    return;
  }
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  if (card_ >= 0 && y > renderer.getScreenHeight() - 180) {
    if (x < renderer.getScreenWidth() / 2)
      showMeaning_ = !showMeaning_;
    else
      history_ = true;
    requestUpdate();
  } else if (history_ || showMeaning_) {
    history_ = false;
    showMeaning_ = false;
    requestUpdate();
  } else
    drawNext();
}

void TarotActivity::drawTextCard() {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();

  if (card_ < 0) {
    // No deck yet: say so instead of inviting a draw that cannot happen.
    renderer.drawRect(0, 0, w, h, true);
    renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 48, tr(STR_TAROT), true);
    if (TarotAssets::installed())
      drawBottomPanel(tr(STR_TAROT_DRAW_HINT), 2);
    else
      drawBottomPanel(tr(STR_TAROT_NEEDS_DOWNLOAD), 3);
    return;
  }

  const TarotMeaning meaning = assets_.meaning(card_);
  if (TarotAssets::installed() && tarot::drawCard(renderer, card_, 0, 0, w, h)) {
    if (showMeaning_)
      drawBottomPanel(tarotMeaningLocalized(card_, meaning.meaning.c_str()), 5);
    else
      drawBottomPanel(tr(STR_TAROT_CONTROL_HINT), 2);
    return;
  }

  // Bitmap missing or unreadable: fall back to a text-only card.
  renderer.clearScreen();
  renderer.drawRect(0, 0, w, h, true);
  char title[32];
  std::snprintf(title, sizeof(title), tr(STR_TAROT_CARD_FORMAT), card_ + 1);
  renderer.drawCenteredText(toybox::kUiFontId, 108, title, true);
  renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 45, meaning.name.c_str(), true);
  if (showMeaning_)
    drawBottomPanel(tarotMeaningLocalized(card_, meaning.meaning.c_str()), 5);
  else
    drawBottomPanel(tr(STR_TAROT_CONTROL_HINT), 2);
}

void TarotActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (history_) {
    renderer.drawCenteredText(toybox::kDisplayFontId, 48, tr(STR_TAROT_HISTORY), true);
    const auto& history = deck_.history();
    const int lineHeight = renderer.getLineHeight(kProseFontId);
    int y = 132;
    for (size_t i = 0; i < history.size() && i < 12; ++i, y += lineHeight) {
      const TarotMeaning meaning = assets_.meaning(history[i]);
      char line[64];
      std::snprintf(line, sizeof(line), "%2u.  %s", static_cast<unsigned>(i + 1), meaning.name.c_str());
      renderer.drawText(kProseFontId, kPanelMargin + 12, y, line, true);
    }
    renderer.drawCenteredText(kProseFontId, renderer.getScreenHeight() - 48, tr(STR_TAROT_RETURN_HINT), true);
  } else
    drawTextCard();
  renderer.displayBuffer();
}
