#pragma once

// X4 host/Game Master for Cards Against Whatever. The phone is ONLY a
// controller; the authoritative game never leaves this Activity.
#include <memory>
#include "../../activities/Activity.h"
#include "CardPartyCore.h"
#include "CardPartySessions.h"
#include "CardPartyDemoDeck.h"
#include "web/CardPartyBrowserServer.h"

class CardPartyActivity final : public Activity {
 public:
  CardPartyActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("CardParty",renderer,input),
      sessions_(game_), server_(game_,sessions_,cardparty::demoDeck()) {}
  ~CardPartyActivity() override = default;
  static std::unique_ptr<Activity> create(GfxRenderer&,MappedInputManager&);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return server_.running(); }
  bool skipLoopDelay() override { return server_.running(); }

 private:
  enum class Stage : uint8_t { Choice, Hosted, Failed };
  void resetGame();
  void start(cardparty::web::Server::Mode mode);
  void handleTap(int x,int y);
  void drawBase(const char* section);
  void drawChoice();
  void drawLobby();
  void drawGame();
  void drawError();
  void drawPrompt(const char* sentence,int x,int y,int maxChars);
  void drawDiagnostics();
  bool matchInProgress() const;
  void leave();

  cardparty::Game game_;
  cardparty::Sessions sessions_;
  cardparty::web::Server server_;
  Stage stage_=Stage::Choice;
  bool ownsOrientation_=false;
  uint32_t fingerprint_=0;
  bool abortArmed_=false;
  uint32_t abortArmedAt_=0;
  bool backArmed_=false;
  uint32_t backArmedAt_=0;
  char report_[90]={};
};
