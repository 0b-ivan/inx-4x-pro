#pragma once

#include <memory>
#include <Memory.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <string>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

// A fully functional AP/DNS/HTTP lab portal with conspicuous test labelling.
// It never impersonates production WiFi or accepts credential submissions.
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
  enum State { SELECT_TEMPLATE, ENTER_SSID, RUNNING, STOPPED };
  enum Template { LANDING, FORM_TEST, DIAGNOSTICS, TEMPLATE_COUNT };
  State state = SELECT_TEMPLATE;
  int selectorIndex = 0;
  Template selectedTemplate = LANDING;
  ButtonNavigator buttonNavigator;
  std::string apSsid = "X4PRO-PORTAL";
  int pageViews = 0;
  int testSubmissions = 0;
  std::unique_ptr<WebServer> webServer;
  std::unique_ptr<DNSServer> dnsServer;

  void chooseTemplate();
  void startPortal();
  void stopPortal();
  void handleRoot();
  void handleTestSubmission();
  void handleStatus();
  void handleNotFound();
};
