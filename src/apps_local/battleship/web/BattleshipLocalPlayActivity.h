#pragma once

#include "../../../activities/Activity.h"
#include "../../ui/ToyboxScreen.h"
#include "BattleshipBrowserServer.h"

// The transport chooser behind Battleship's existing PLAY NEARBY row.
// X4 Pro returns immediately to the existing ESP-NOW LinkActivity path;
// Browser stays here and owns Wi-Fi + the small HTTP server until Back.
class BattleshipLocalPlayActivity final : public Activity {
 public:
  BattleshipLocalPlayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BattleshipLocalPlay", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return browser_.running(); }
  bool preventAutoSleep() override { return browser_.running(); }

 private:
  enum class Stage : uint8_t { Transport, Network, BrowserWaiting };

  void routeChoiceInput();
  void chooseTransport(int index);
  void chooseNetwork(int index);
  void startWifiBrowser();
  void startHotspotBrowser();
  void enterBrowserWaiting();
  void cleanupBrowserNetwork();
  void finishCancelled();

  void drawTransportChoice();
  void drawNetworkChoice();
  void drawBrowserWaiting();

  Stage stage_ = Stage::Transport;
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
  int selected_ = 0;

  bshipweb::Server browser_;
  bool devModePaused_ = false;
  bool ownsSta_ = false;
  bool lastClientSeen_ = false;
};
