#pragma once
#include <memory>
#include <Memory.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <string>

#include "../../activities/Activity.h"

class CaptivePortalActivity final : public Activity {
 public:
  explicit CaptivePortalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CaptivePortal", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<CaptivePortalActivity>(r, m);
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == RUNNING; }
  bool skipLoopDelay() override { return state == RUNNING; }

 private:
  // Deliberately a local, credential-free portal demo. Never collect passwords.
  enum State { READY, RUNNING, STOPPED };
  State state = READY;
  int pageViews = 0;
  std::unique_ptr<WebServer> webServer;
  std::unique_ptr<DNSServer> dnsServer;

  void startPortal();
  void stopPortal();
  void handleRoot();
  void handleNotFound();
};
