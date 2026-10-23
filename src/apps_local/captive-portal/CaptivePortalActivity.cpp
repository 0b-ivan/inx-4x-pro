#include "CaptivePortalActivity.h"

#include "../Shelf.h"
#include "../util/RadioManager.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <Logging.h>
#include <WiFi.h>

// Isolated local test page only. No login forms, passwords, user templates,
// session collection, or connection-error pretexts.
namespace {
constexpr char TEST_SSID[] = "X4PRO-PORTAL";
constexpr char LANDING_HTML[] = R"html(<!doctype html><html lang="en"><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="utf-8"><title>X4 Pro Portal Test</title>
<style>body{font:18px system-ui;max-width:32rem;margin:3rem auto;padding:1rem}
section{border:2px solid #222;border-radius:12px;padding:1.5rem}
small{color:#555}</style></head><body><section>
<h1>X4 Pro test portal</h1><p>The local access point and HTTP page are working.</p>
<p>No internet access is provided by this network.</p>
<small>Laboratory preview: no passwords or personal data requested.</small>
</section></body></html>)html";
}  // namespace

void CaptivePortalActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = READY;
  pageViews = 0;
  RADIO.ensureWifi();
  requestUpdate();
}

void CaptivePortalActivity::onExit() {
  stopPortal();
  RADIO.shutdown();
  devmode::resume();
  Activity::onExit();
}

void CaptivePortalActivity::startPortal() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(TEST_SSID)) {
    LOG_ERR("PORTAL", "Cannot start local test AP");
    state = STOPPED;
    requestUpdate();
    return;
  }
  dnsServer = std::make_unique<DNSServer>();
  dnsServer->start(53, "*", WiFi.softAPIP());
  webServer = std::make_unique<WebServer>(80);
  webServer->on("/", HTTP_GET, [this] { handleRoot(); });
  webServer->onNotFound([this] { handleNotFound(); });
  webServer->begin();
  state = RUNNING;
  LOG_INF("PORTAL", "Local test portal ready at 192.168.4.1");
  requestUpdate();
}

void CaptivePortalActivity::stopPortal() {
  if (webServer) {
    webServer->stop();
    webServer.reset();
  }
  if (dnsServer) {
    dnsServer->stop();
    dnsServer.reset();
  }
  WiFi.softAPdisconnect(true);
}

void CaptivePortalActivity::handleRoot() {
  if (!webServer) return;
  ++pageViews;
  webServer->send(200, "text/html", LANDING_HTML);
  requestUpdate();
}

void CaptivePortalActivity::handleNotFound() {
  if (!webServer) return;
  webServer->sendHeader("Location", "http://192.168.4.1/", true);
  webServer->send(302, "text/plain", "");
}

void CaptivePortalActivity::loop() {
  if (state == RUNNING) {
    if (dnsServer) dnsServer->processNextRequest();
    if (webServer) webServer->handleClient();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      stopPortal(); state = STOPPED; requestUpdate(); return;
    }
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y) &&
        GUI.tappedAlwaysHintButton(x, y, renderer) == 1) {
      stopPortal(); state = STOPPED; requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (state == STOPPED) {
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startPortal(); return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const int btn = GUI.tappedAlwaysHintButton(x, y, renderer);
    if (btn == 1) { shelf::leave(renderer, mappedInput); return; }
    if (btn == 2) { startPortal(); return; }
    const auto& m = UITheme::getInstance().getMetrics();
    const int rowTop = m.topPadding + m.headerHeight + m.verticalSpacing;
    int touched = -1;
    const auto result = mappedInput.rowTouch(touched, rowTop, GUI.getListRowStep(false), 1,
                                             0, renderer.getScreenWidth(), m.listRowHeight);
    if (result == MappedInputManager::RowTouch::Tap && touched == 0) {
      startPortal();
    }
  }
}

void CaptivePortalActivity::render(RenderLock&&) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, "Captive Portal", "Local Test");

  if (state == READY) {
    const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
    GUI.drawList(renderer, Rect{0, top, w,
        h - top - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing},
        1, 0, [](int) -> std::string { return "Start X4 Pro test portal"; });
    const auto labels = mappedInput.mapLabels("Back", "Start", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == RUNNING) {
    int y = m.topPadding + m.headerHeight + 30;
    renderer.drawCenteredText(UI_12_FONT_ID, y, TEST_SSID, true, EpdFontFamily::BOLD);
    y += 65;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Connect to the test WiFi.");
    y += 48;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Open http://192.168.4.1/");
    y += 48;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "No login or internet access.");
    y += 48;
    char count[36];
    snprintf(count, sizeof(count), "Page views: %d", pageViews);
    renderer.drawCenteredText(UI_10_FONT_ID, y, count);
    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2, "Portal stopped");
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 48, "Tap to return.");
  }
  renderer.displayBuffer();
}
