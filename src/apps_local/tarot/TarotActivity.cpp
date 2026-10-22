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
constexpr int kBarHeight = 72;
}  // namespace

// Draws an optional bold title and wrapped body on an opaque panel anchored to
// the bottom of the screen, so prose stays legible over a card image instead of
// running off its edges. A null title draws body only (hints).
void TarotActivity::drawBottomPanel(const char* title, const char* body, const int maxLines, const int bottomInset) {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int textWidth = w - 2 * kPanelMargin - 2 * kPanelPad;
  const std::vector<std::string> lines = renderer.wrappedText(kProseFontId, body, textWidth, maxLines);
  const bool hasTitle = title != nullptr && title[0] != '\0';
  if (lines.empty() && !hasTitle) return;

  const int lineHeight = renderer.getLineHeight(kProseFontId);
  const int titleHeight = hasTitle ? lineHeight + 6 : 0;
  const int panelHeight = titleHeight + static_cast<int>(lines.size()) * lineHeight + 2 * kPanelPad;
  const int panelY = h - bottomInset - panelHeight - kPanelMargin;
  renderer.fillRect(kPanelMargin, panelY, w - 2 * kPanelMargin, panelHeight, false);
  renderer.drawRect(kPanelMargin, panelY, w - 2 * kPanelMargin, panelHeight, true);

  int y = panelY + kPanelPad;
  if (hasTitle) {
    renderer.drawCenteredText(kProseFontId, y, title, true, EpdFontFamily::BOLD);
    y += titleHeight;
  }
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
  faceUp_ = false;
  showMeaning_ = false;
  history_ = false;
  requestUpdate();
}

void TarotActivity::drawNext() {
  // A card is dealt face down; the player turns it over to read it, so the back
  // is what you see at the moment of dealing.
  card_ = deck_.drawNext();
  faceUp_ = false;
  showMeaning_ = false;
  history_ = false;
  requestUpdate();
}

void TarotActivity::reveal() {
  faceUp_ = true;
  showMeaning_ = false;
  requestUpdate();
}

// Tapping the card, or the left bar zone: deal the first card, then turn the
// dealt card over, then toggle its meaning on repeat taps.
void TarotActivity::cardAction() {
  if (card_ < 0)
    drawNext();
  else if (!faceUp_)
    reveal();
  else {
    showMeaning_ = !showMeaning_;
    requestUpdate();
  }
}

void TarotActivity::openHistory() {
  if (card_ < 0) return;
  history_ = true;
  showMeaning_ = false;
  requestUpdate();
}

// Three labelled touch zones across the bottom, so every action is visible and
// has a target instead of relying on a remembered half of the screen.
void TarotActivity::drawControlBar(const char* left, const char* mid, const char* right) {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int barY = h - kBarHeight;
  renderer.fillRect(0, barY, w, kBarHeight, false);
  renderer.drawLine(0, barY, w - 1, barY, 2, true);
  const int third = w / 3;
  renderer.drawLine(third, barY, third, h - 1, 1, true);
  renderer.drawLine(2 * third, barY, 2 * third, h - 1, 1, true);
  const int textY = barY + (kBarHeight - renderer.getLineHeight(kProseFontId)) / 2;
  const auto label = [&](const char* text, int zoneX, int zoneW) {
    if (text == nullptr || text[0] == '\0') return;
    const int textW = renderer.getTextWidth(kProseFontId, text);
    renderer.drawText(kProseFontId, zoneX + (zoneW - textW) / 2, textY, text, true);
  };
  label(left, 0, third);
  label(mid, third, third);
  label(right, 2 * third, w - 2 * third);
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
    if (history_) {
      history_ = false;
      requestUpdate();
    } else if (showMeaning_) {
      showMeaning_ = false;
      requestUpdate();
    } else
      shelf::leave(renderer, mappedInput);
    return;
  }

  // In the history list any press or tap returns to the card.
  if (history_) {
    int hx = 0, hy = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right) || mappedInput.wasScreenTapped(hx, hy)) {
      history_ = false;
      requestUpdate();
    }
    return;
  }

  // Buttons: Confirm is the card action (deal/reveal/meaning), Right deals the
  // next card, Left opens the history.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    cardAction();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    drawNext();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    openHistory();
    return;
  }

  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  // The deck view (no card yet) has no bar; a tap anywhere deals.
  if (card_ < 0) {
    drawNext();
    return;
  }
  const int barY = renderer.getScreenHeight() - kBarHeight;
  if (y >= barY) {
    const int third = renderer.getScreenWidth() / 3;
    if (x < third)
      cardAction();  // reveal, or toggle meaning
    else if (x < 2 * third)
      drawNext();
    else
      openHistory();
    return;
  }
  // A tap on the card itself reveals it, then toggles its meaning.
  cardAction();
}

void TarotActivity::drawTextCard() {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();

  if (card_ < 0) {
    // The face-down deck, drawn procedurally so it shows even without assets.
    tarot::drawCardBack(renderer, 0, 0, w, h);
    // "Tarot" on a white band in the upper third, clear of the centre emblem.
    const char* title = tr(STR_TAROT);
    const int tw = renderer.getTextWidth(toybox::kDisplayFontId, title);
    const int th = renderer.getLineHeight(toybox::kDisplayFontId);
    const int ty = h / 3;
    renderer.fillRect((w - tw) / 2 - 18, ty - 10, tw + 36, th + 20, false);
    renderer.drawRect((w - tw) / 2 - 18, ty - 10, tw + 36, th + 20, 1, true);
    renderer.drawCenteredText(toybox::kDisplayFontId, ty, title, true);
    if (TarotAssets::installed())
      drawBottomPanel(nullptr, tr(STR_TAROT_DRAW_HINT), 2);
    else
      drawBottomPanel(nullptr, tr(STR_TAROT_NEEDS_DOWNLOAD), 3);
    return;
  }

  // A dealt card the player has not turned over yet: show the back.
  if (!faceUp_) {
    tarot::drawCardBack(renderer, 0, 0, w, h);
    drawControlBar(tr(STR_TAROT_TAB_REVEAL), tr(STR_TAROT_TAB_DRAW), tr(STR_TAROT_TAB_HISTORY));
    return;
  }

  const TarotMeaning meaning = assets_.meaning(card_);
  if (!TarotAssets::installed() || !tarot::drawCard(renderer, card_, 0, 0, w, h)) {
    // Bitmap missing or unreadable: fall back to a text-only card.
    renderer.clearScreen();
    renderer.drawRect(0, 0, w, h, true);
    renderer.drawCenteredText(kProseFontId, h / 2 - 45, tarotNameLocalized(card_, meaning.name.c_str()), true,
                              EpdFontFamily::BOLD);
  }
  if (showMeaning_)
    drawBottomPanel(tarotNameLocalized(card_, meaning.name.c_str()),
                    tarotMeaningLocalized(card_, meaning.meaning.c_str()), 4, kBarHeight);
  drawControlBar(tr(STR_TAROT_TAB_MEANING), tr(STR_TAROT_TAB_DRAW), tr(STR_TAROT_TAB_HISTORY));
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
      std::snprintf(line, sizeof(line), "%2u.  %s", static_cast<unsigned>(i + 1),
                    tarotNameLocalized(history[i], meaning.name.c_str()));
      renderer.drawText(kProseFontId, kPanelMargin + 12, y, line, true);
    }
    renderer.drawCenteredText(kProseFontId, renderer.getScreenHeight() - 48, tr(STR_TAROT_RETURN_HINT), true);
  } else
    drawTextCard();
  renderer.displayBuffer();
}
