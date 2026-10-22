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
  void reveal();
  void cardAction();
  void openHistory();
  void startDownload();
  void drawTextCard();
  void drawControlBar(const char* left, const char* mid, const char* right);
  void drawBottomPanel(const char* title, const char* body, int maxLines, int bottomInset = 0);
  TarotDeck deck_;
  TarotAssets assets_;
  int card_ = -1;
  bool faceUp_ = false;
  bool showMeaning_ = false;
  bool history_ = false;
  bool awaitRelease_ = false;
};
