#pragma once

#include "../../../activities/Activity.h"
#include "../../../components/UITheme.h"
#include "../../ui/ToyboxScreen.h"
#include "../BattleshipCore.h"
#include "BattleshipBrowserServer.h"
#include "BrowserPlayer.h"

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
  enum class Stage : uint8_t { Transport, Network, BrowserWaiting, X4Placement, Playing };

  struct GridGeometry {
    int originX = 0;
    int originY = 0;
    int cell = 0;
  };

  void routeChoiceInput();
  void routeX4PlacementInput();
  void chooseTransport(int index);
  void chooseNetwork(int index);
  void startWifiBrowser();
  void startHotspotBrowser();
  void enterBrowserWaiting();
  void startX4Placement();
  void commitX4Fleet();
  void cleanupBrowserNetwork();
  void finishCancelled();

  GridGeometry x4PlaceGrid() const;
  static int cellAt(const GridGeometry& grid, int x, int y);
  static Rect cellRect(const GridGeometry& grid, int cell);
  Rect x4PlaceRosterRect() const;
  int x4PlaceRosterRowAt(int x, int y) const;
  void selectX4Ship(int shipIndex);
  void rotateX4Ship();
  void moveX4Ship(int cell);
  void handleX4PlaceTap(int cell);
  void shuffleX4Fleet();

  void drawTransportChoice();
  void drawNetworkChoice();
  void drawBrowserWaiting();
  void drawX4Placement();
  void drawPlaying();
  void drawX4PlaceGrid();
  void drawX4PlaceRoster();

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
  char x4Status_[48] = {};
  bool devModePaused_ = false;
  bool ownsSta_ = false;
  bool lastClientSeen_ = false;
};
