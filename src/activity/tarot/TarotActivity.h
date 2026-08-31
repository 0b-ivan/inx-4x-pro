#pragma once

#include <functional>

#include "activity/ActivityWithSubactivity.h"
#include "TarotAssets.h"
#include "TarotDeck.h"

class TarotActivity final : public ActivityWithSubactivity {
 public:
  TarotActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : ActivityWithSubactivity("Tarot", renderer, mappedInput), onBack_(std::move(onBack)) {}
  void onEnter() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;
  bool prioritizesScreenTouch() const override { return true; }

 private:
  enum class View { Prompt, Card, History };
  View view_ = View::Prompt;
  TarotDeck deck_;
  TarotAssets assets_;
  std::function<void()> onBack_;
  int card_ = -1;
  bool showMeaning_ = false;
  int historyPage_ = 0;

  void drawNext();
  void goBack();
  void render();
  void renderPrompt();
  void renderCard();
  void renderHistory();
  void renderWrapped(const std::string& text, int x, int y, int width, int maxLines);
};
