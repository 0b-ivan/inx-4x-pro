#pragma once
#include <memory>
#include "../../activities/Activity.h"
#include "TarotAssets.h"
#include "TarotDeck.h"

class TarotActivity final : public Activity {
 public:
  TarotActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Tarot", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
 private:
  void drawNext();
  void drawTextCard();
  TarotDeck deck_;
  TarotAssets assets_;
  int card_ = -1;
  bool showMeaning_ = false;
  bool history_ = false;
};
