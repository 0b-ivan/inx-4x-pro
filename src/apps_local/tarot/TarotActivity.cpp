#include "TarotActivity.h"
#include <GfxRenderer.h>
#include <Bitmap.h>
#include <HalStorage.h>
#include <cstdio>
#include <Memory.h>
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "TarotDownloadActivity.h"
#include "../../activities/network/WifiSelectionActivity.h"

std::unique_ptr<Activity> TarotActivity::create(GfxRenderer& r, MappedInputManager& i) {
  return makeUniqueNoThrow<TarotActivity>(r, i);
}

void TarotActivity::onEnter() {
  toybox::ensureFonts(renderer);
  assets_.load();
  deck_.shuffle();
  card_ = -1; showMeaning_ = false; history_ = false;
  requestUpdate();
}

void TarotActivity::drawNext() {
  card_ = deck_.drawNext(); showMeaning_ = false; history_ = false; requestUpdate();
}

void TarotActivity::loop() {
  if (card_ < 0 && !TarotAssets::installed() &&
      (mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      startActivityForResult(std::make_unique<TarotDownloadActivity>(renderer, mappedInput), [this](const ActivityResult&) {
        assets_.load(); requestUpdate();
      });
    });
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (history_ || showMeaning_) { history_ = false; showMeaning_ = false; requestUpdate(); }
    else shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (card_ >= 0) { history_ = true; showMeaning_ = false; requestUpdate(); }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (card_ >= 0) showMeaning_ = !showMeaning_;
    requestUpdate(); return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) { drawNext(); return; }
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  if (card_ < 0 && !TarotAssets::installed()) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput), [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      startActivityForResult(std::make_unique<TarotDownloadActivity>(renderer, mappedInput), [this](const ActivityResult&) {
        assets_.load(); requestUpdate();
      });
    });
    return;
  }
  if (card_ >= 0 && y > renderer.getScreenHeight() - 180) {
    if (x < renderer.getScreenWidth() / 2) showMeaning_ = !showMeaning_;
    else history_ = true;
    requestUpdate();
  } else if (history_ || showMeaning_) { history_ = false; showMeaning_ = false; requestUpdate(); }
  else drawNext();
}

void TarotActivity::drawTextCard() {
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  if (card_ >= 0 && TarotAssets::installed()) {
    HalFile file;
    if (Storage.openFileForRead("TAROT", TarotAssets::cardPath(card_).c_str(), file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        // Tarot card artwork uses the same aspect ratio as the X4 Pro panel.
        // Match the main-branch renderer: cards own the complete framebuffer,
        // with no inset card frame or footer stealing display area.
        renderer.drawBitmap(bitmap, 0, 0, w, h);
        file.close();
        const TarotMeaning meaning = assets_.meaning(card_);
        if (showMeaning_)
          renderer.drawCenteredText(toybox::kUiFontId, h - 116, meaning.meaning.c_str(), true);
        return;
      }
      file.close();
    }
  }
  renderer.drawRect(0, 0, w, h, true);
  if (card_ < 0) {
    renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 24, "TAROT", true);
    renderer.drawCenteredText(toybox::kUiFontId, h / 2 + 30, "TAP OR SWIPE RIGHT TO DRAW", true);
    return;
  }
  const TarotMeaning meaning = assets_.meaning(card_);
  char title[32]; std::snprintf(title, sizeof(title), "CARD %d", card_ + 1);
  renderer.drawCenteredText(toybox::kUiFontId, 108, title, true);
  renderer.drawCenteredText(toybox::kDisplayFontId, h / 2 - 45, meaning.name.c_str(), true);
  if (showMeaning_)
    renderer.drawCenteredText(toybox::kUiFontId, h / 2 + 26, meaning.meaning.c_str(), true);
  else
    renderer.drawCenteredText(toybox::kUiFontId, h - 116, "TAP LEFT: MEANING   TAP RIGHT: HISTORY", true);
}

void TarotActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (history_) {
    renderer.drawCenteredText(toybox::kDisplayFontId, 56, "TAROT HISTORY", true);
    const auto& history = deck_.history();
    for (size_t i = 0; i < history.size() && i < 12; ++i) {
      char line[32]; std::snprintf(line, sizeof(line), "%2u. CARD %d", static_cast<unsigned>(i + 1), history[i] + 1);
      renderer.drawText(toybox::kUiFontId, 70, 120 + static_cast<int>(i) * 42, line, true);
    }
    renderer.drawCenteredText(toybox::kUiFontId, renderer.getScreenHeight() - 54, "TAP OR BACK TO RETURN", true);
  } else drawTextCard();
  renderer.displayBuffer();
}
