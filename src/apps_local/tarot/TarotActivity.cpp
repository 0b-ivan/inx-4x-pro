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
  if (card_ < 0 && !TarotAssets::installed() && (mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
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
  if (card_ < 0 && !TarotAssets::installed()) {
    startDownload();
    return;
  }
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
  if (card_ >= 0 && TarotAssets::installed()) {
    if (tarot::drawCard(renderer, card_, 0, 0, w, h)) {
      const TarotMeaning meaning = assets_.meaning(card_);
      if (showMeaning_)
        renderer.drawCenteredText(toybox::kUiFontId, h - 116, tarotMeaningLocalized(card_, meaning.meaning.c_str()),
                                  true);
      return;
    }
    renderer.clearScreen();
  }
  renderer.drawRect(0, 0, w, h, true);
  if (card_ < 0) {
    renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 24, tr(STR_TAROT), true);
    renderer.drawCenteredText(toybox::kUiFontId, h / 2 + 30, tr(STR_TAROT_DRAW_HINT), true);
    return;
  }
  const TarotMeaning meaning = assets_.meaning(card_);
  char title[32];
  std::snprintf(title, sizeof(title), tr(STR_TAROT_CARD_FORMAT), card_ + 1);
  renderer.drawCenteredText(toybox::kUiFontId, 108, title, true);
  renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 45, meaning.name.c_str(), true);
  if (showMeaning_)
    renderer.drawCenteredText(toybox::kUiFontId, h / 2 + 26, tarotMeaningLocalized(card_, meaning.meaning.c_str()),
                              true);
  else
    renderer.drawCenteredText(toybox::kUiFontId, h - 116, tr(STR_TAROT_CONTROL_HINT), true);
}

void TarotActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (history_) {
    renderer.drawCenteredText(toybox::kDisplayFontId, 56, tr(STR_TAROT_HISTORY), true);
    const auto& history = deck_.history();
    for (size_t i = 0; i < history.size() && i < 12; ++i) {
      char line[32];
      std::snprintf(line, sizeof(line), tr(STR_TAROT_HISTORY_CARD_FORMAT), static_cast<unsigned>(i + 1),
                    history[i] + 1);
      renderer.drawText(toybox::kUiFontId, 70, 120 + static_cast<int>(i) * 42, line, true);
    }
    renderer.drawCenteredText(toybox::kUiFontId, renderer.getScreenHeight() - 54, tr(STR_TAROT_RETURN_HINT), true);
  } else
    drawTextCard();
  renderer.displayBuffer();
}
