#pragma once
#include "../../../activities/Activity.h"
#include "ConnectFourBrowserServer.h"

// Independent activity: the solo and ESP-NOW ConnectFourActivity/LinkActivity
// lifecycle remains unchanged. This only owns a temporary protected hotspot.
class ConnectFourBrowserActivity final : public Activity {
 public:
  ConnectFourBrowserActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("ConnectFourBrowser", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return server_.running(); }
  bool preventAutoSleep() override { return server_.running(); }
 private:
  void drawJoin();
  void drawGame();
  c4web::Server server_;
  bool failed_ = false;
  bool shownJoined_ = false;
  bool shownConnected_ = false;
};
