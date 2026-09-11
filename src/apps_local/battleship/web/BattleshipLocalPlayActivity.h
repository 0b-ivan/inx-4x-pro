#pragma once

#include "../../../activities/Activity.h"
#include "../../../components/UITheme.h"
#include "../../player/PlayerName.h"
#include "../../ui/ToyboxScreen.h"
#include "../BattleshipCore.h"
#include "BattleshipBrowserServer.h"
#include "BrowserPlayer.h"

class BattleshipLocalPlayActivity final : public Activity {
 public:
  BattleshipLocalPlayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BattleshipLocalPlay", renderer, mappedInput) {
    // The reader already has one device-wide Player identity. Reuse it for the
    // browser opponent instead of presenting the hardware model as a player.
    browser_.setOpponentName(player::name());
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return browser_.running(); }
  bool preventAutoSleep() override { return browser_.running(); }

 private:
  enum class Stage : uint8_t { Transport, Network, BrowserWaiting, X4Placement, Playing };

  struct GridGeometry {
    int originX = 0;
    int originY = 0;
    int cell = 0;
  };

  void routeChoiceInput();
  void routeX4PlacementInput();
  void routePlayingInput();
  void chooseTransport(int index);
  void chooseNetwork(int index);
  void startWifiBrowser();
  void startHotspotBrowser();
  void enterBrowserWaiting();
  void startX4Placement();
  void commitX4Fleet();
  void startMatchIfReady();
  void surrenderX4();
  void rematchFromX4();
  void cleanupBrowserNetwork();
  void finishCancelled();

  GridGeometry x4PlaceGrid() const;
  GridGeometry targetGrid() const;
  GridGeometry ownFleetGrid() const;
  static int cellAt(const GridGeometry& grid, int x, int y);
  static Rect cellRect(const GridGeometry& grid, int cell);
  Rect x4PlaceRosterRect() const;
  int x4PlaceRosterRowAt(int x, int y) const;
  void selectX4Ship(int shipIndex);
  void rotateX4Ship();
  void moveX4Ship(int cell);
  void handleX4PlaceTap(int cell);
  void shuffleX4Fleet();
  void aimX4Shot(int cell);
  void fireX4Shot();
  void reportLastShot(bool browserShot);
  void recordMatchIfFinished();

  void drawTransportChoice();
  void drawNetworkChoice();
  void drawBrowserWaiting();
  void drawX4Placement();
  void drawX4PlaceGrid();
  void drawX4PlaceRoster();
  void drawTargetGrid();
  void drawOwnFleetGrid();
  void drawPlaying();

  Stage stage_ = Stage::Transport;
  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
  int selected_ = 0;

  bshipweb::BrowserPlayer browserPlayer_;
  bship::Game browserGame_;
  bship::Fleet x4Fleet_;
  bshipweb::Server browser_;
  Rect x4BodySlot_{};
  uint32_t seed_ = 1;
  int selectedX4Ship_ = -1;
  int x4AimCell_ = -1;
  uint8_t seenLastShot_ = 0;
  char x4Status_[48] = {};
  char x4Report_[48] = {};
  bool x4Ready_ = false;
  bool x4SurrenderArmed_ = false;
  // Browser callbacks and device input can observe the same terminal state on
  // different loop passes. Only the first observation books progression for
  // the local X4 identity; the browser opponent is never materialized as a
  // persistent local Player merely to account for the result.
  bool resultRecorded_ = false;
  bool devModePaused_ = false;
  bool ownsSta_ = false;
  bool lastClientSeen_ = false;
};
