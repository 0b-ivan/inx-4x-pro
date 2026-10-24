#include "CaptivePortalActivity.h"
#include "PortalLabRules.h"

#include "../Shelf.h"
#include "../util/RadioManager.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

// No login impersonation, real passwords, arbitrary HTML injection or credential
// logging. The selected pages exercise real SoftAP/DNS/HTTP and form routing.
// They are explicitly marked as laboratory demonstrations.
namespace {
constexpr char TEST_SSID[] = "X4PRO-PORTAL";
constexpr const char* TEMPLATE_NAMES[] = {
    "Start page", "Synthetic form test", "HTTP diagnostics"
};
constexpr char LANDING_HTML[] = R"html(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>X4 Pro Lab Portal</title>
<style>body{font:18px system-ui;max-width:34rem;margin:3rem auto;padding:1rem}
main{border:2px solid #222;border-radius:12px;padding:1.5rem}
small{color:#555}a{color:#0645ad}</style></head><body><main>
<h1>X4 Pro Laboratory Portal</h1>
<p>This is a local access-point, DNS and HTTP connectivity test.</p>
<p>There is no internet connection, login requirement or credential collection.</p>
<p><a href="/status">Server status</a></p>
<small>LAB TEST - no real usernames, passwords or personal information.</small>
</main></body></html>)html";

constexpr char FORM_HTML[] = R"html(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>X4 Pro Synthetic Form Test</title>
<style>body{font:18px system-ui;max-width:34rem;margin:3rem auto;padding:1rem}
main{border:2px solid #222;border-radius:12px;padding:1.5rem}
input,button{font:inherit;padding:.7rem;margin:.4rem 0;max-width:100%}
small{color:#555}</style></head><body><main>
<h1>LAB ONLY - Synthetic Form</h1>
<p>Tests browser-to-device HTTP POST. Use a fabricated code such as X4TEST-DEMO1.</p>
<form method="POST" action="/check">
<label>Fictional test code (not an account or secret)
<input name="test_id" type="text" maxlength="24" pattern="X4TEST-[A-Za-z0-9]{1,17}"
placeholder="X4TEST-DEMO1" required autocomplete="off"></label>
<button type="submit">Submit test event</button></form>
<small>No passwords, email addresses or submitted codes are saved or displayed.</small>
</main></body></html>)html";

constexpr char DIAGNOSTICS_HTML[] = R"html(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>X4 Pro HTTP Diagnostics</title>
<style>body{font:18px system-ui;max-width:34rem;margin:3rem auto;padding:1rem}
main{border:2px solid #222;border-radius:12px;padding:1.5rem}</style></head><body><main>
<h1>X4 Pro Diagnostics - LAB</h1>
<p>Local HTTP server reachable. DNS wildcard routes to the test access point.</p>
<p><a href="/status">JSON server status</a></p>
<p>For an HTTP 404 test, submit a POST request to a nonexistent endpoint.</p>
<p>No external internet access and no user-account authentication.</p>
</main></body></html>)html";

constexpr char SUCCESS_HTML[] =
    "<!doctype html><html><meta charset=\"utf-8\"><title>Test complete</title>"
    "<body><h1>LAB HTTP POST accepted</h1>"
    "<p>The fictional test identifier was validated but not retained.</p>"
    "<a href=\"/\">Back to laboratory portal</a></body></html>";
constexpr char REJECT_HTML[] =
    "<!doctype html><html><meta charset=\"utf-8\"><title>Invalid test</title>"
    "<body><h1>Invalid laboratory test identifier</h1>"
    "<p>Use a fabricated X4TEST- code with letters or numbers only.</p></body></html>";
}  // namespace

void CaptivePortalActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  state = SELECT_TEMPLATE;
  selectorIndex = 0;
  selectedTemplate = LANDING;
  apSsid = TEST_SSID;
  pageViews = 0;
  testSubmissions = 0;
  RADIO.ensureWifi();
  requestUpdate();
}

void CaptivePortalActivity::onExit() {
  stopPortal();
  RADIO.shutdown();
  devmode::resume();
  Activity::onExit();
}

void CaptivePortalActivity::chooseTemplate() {
  if (selectorIndex < 0 || selectorIndex >= TEMPLATE_COUNT) return;
  selectedTemplate = static_cast<Template>(selectorIndex);
  state = ENTER_SSID;
  requestUpdate();
  startActivityForResult(
      makeUniqueNoThrow<KeyboardEntryActivity>(
          renderer, mappedInput, "LAB SSID suffix (optional)", "", 12),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          state = SELECT_TEMPLATE;
          requestUpdate();
          return;
        }
        const std::string suffix = std::get<KeyboardResult>(result.data).text;
        if (!suffix.empty() && !portal_lab::validSuffix(suffix)) {
          LOG_ERR("PORTAL", "Invalid laboratory SSID suffix");
          state = SELECT_TEMPLATE;
          requestUpdate();
          return;
        }
        apSsid = portal_lab::ssidFor(suffix);
        startPortal();
      });
}

void CaptivePortalActivity::startPortal() {
  // The app may be entered more than once. Release every old handler first.
  stopPortal();
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apSsid.c_str())) {
    LOG_ERR("PORTAL", "Local laboratory access point startup failed");
    state = STOPPED;
    requestUpdate();
    return;
  }

  dnsServer = makeUniqueNoThrow<DNSServer>();
  webServer = makeUniqueNoThrow<WebServer>(80);
  if (!dnsServer || !webServer) {
    LOG_ERR("PORTAL", "Insufficient memory for portal server");
    stopPortal();
    state = STOPPED;
    requestUpdate();
    return;
  }
  dnsServer->start(53, "*", WiFi.softAPIP());
  webServer->on("/", HTTP_GET, [this] { handleRoot(); });
  webServer->on("/check", HTTP_POST, [this] { handleTestSubmission(); });
  webServer->on("/status", HTTP_GET, [this] { handleStatus(); });
  webServer->onNotFound([this] { handleNotFound(); });
  webServer->begin();

  pageViews = 0;
  testSubmissions = 0;
  state = RUNNING;
  LOG_INF("PORTAL", "X4 Pro laboratory portal active on 192.168.4.1");
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
  const char* html = LANDING_HTML;
  if (selectedTemplate == FORM_TEST) html = FORM_HTML;
  if (selectedTemplate == DIAGNOSTICS) html = DIAGNOSTICS_HTML;
  webServer->send(200, "text/html; charset=utf-8", html);
  requestUpdate();
}

void CaptivePortalActivity::handleTestSubmission() {
  if (!webServer) return;
  // A malformed or unrelated POST must never be accepted or logged.
  // In particular, unexpected account/password fields or extra fields are rejected.
  const std::string testId = webServer->arg("test_id").c_str();
  if (selectedTemplate != FORM_TEST ||
      !portal_lab::validSyntheticSubmission(
          webServer->args() == 1 ? webServer->argName(0).c_str() : "",
          testId, webServer->args())) {
    webServer->send(400, "text/html; charset=utf-8", REJECT_HTML);
    return;
  }
  ++testSubmissions;
  webServer->send(200, "text/html; charset=utf-8", SUCCESS_HTML);
  requestUpdate();
}

void CaptivePortalActivity::handleStatus() {
  if (!webServer) return;
  char body[120];
  snprintf(body, sizeof(body),
           "{\"service\":\"x4pro-portal-lab\",\"ok\":true,\"views\":%d,\"tests\":%d}",
           pageViews, testSubmissions);
  webServer->send(200, "application/json", body);
}

void CaptivePortalActivity::handleNotFound() {
  if (!webServer) return;
  if (webServer->method() != HTTP_GET) {
    webServer->send(404, "text/plain", "Laboratory endpoint not found");
    return;
  }
  webServer->sendHeader("Location", "http://192.168.4.1/", true);
  webServer->send(302, "text/plain", "");
}

void CaptivePortalActivity::loop() {
  if (state == RUNNING) {
    if (dnsServer) dnsServer->processNextRequest();
    if (webServer) webServer->handleClient();
    const bool back = mappedInput.wasReleased(MappedInputManager::Button::Back);
    int x = 0, y = 0;
    const bool stopTap = mappedInput.wasScreenTapped(x, y) &&
                         GUI.tappedAlwaysHintButton(x, y, renderer) == 1;
    if (back || stopTap) {
      stopPortal();
      state = STOPPED;
      requestUpdate();
    }
    return;
  }

  if (state == ENTER_SSID) return;

  if (state == STOPPED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = SELECT_TEMPLATE;
      requestUpdate();
      return;
    }
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      state = SELECT_TEMPLATE;
      requestUpdate();
    }
    return;
  }

  // Original Biscuit template selection, with real X4 Pro GT911 hit zones.
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, TEMPLATE_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, TEMPLATE_COUNT);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    chooseTemplate();
    return;
  }
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const int button = GUI.tappedAlwaysHintButton(x, y, renderer);
  if (button == 1) { shelf::leave(renderer, mappedInput); return; }
  if (button == 2) { chooseTemplate(); return; }
  if (button == 3) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, TEMPLATE_COUNT);
    requestUpdate();
    return;
  }
  if (button == 4) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, TEMPLATE_COUNT);
    requestUpdate();
    return;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  int touched = -1;
  const auto result = mappedInput.rowTouch(
      touched, top, GUI.getListRowStep(false), TEMPLATE_COUNT, 0,
      renderer.getScreenWidth(), m.listRowHeight);
  if (result != MappedInputManager::RowTouch::None && touched >= 0 &&
      touched < TEMPLATE_COUNT) {
    selectorIndex = touched;
    if (result == MappedInputManager::RowTouch::Tap) {
      chooseTemplate();
    } else {
      requestUpdate();
    }
  }
}

void CaptivePortalActivity::render(RenderLock&&) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight},
                 "Captive Portal", "LAB - X4 Pro");

  if (state == SELECT_TEMPLATE) {
    const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
    GUI.drawList(renderer,
                 Rect{0, top, w,
                      h - top - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing},
                 TEMPLATE_COUNT, selectorIndex,
                 [](int index) -> std::string { return TEMPLATE_NAMES[index]; });
    const auto labels = mappedInput.mapLabels("Back", "Choose", "^", "v");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2,
                              labels.btn3, labels.btn4);
  } else if (state == RUNNING) {
    const int left = m.contentSidePadding;
    int y = m.topPadding + m.headerHeight + 28;
    renderer.drawCenteredText(UI_12_FONT_ID, y, apSsid.c_str(),
                              true, EpdFontFamily::BOLD);
    y += 72;
    renderer.drawText(UI_10_FONT_ID, left, y, "LAB - no real credentials");
    y += 45;
    renderer.drawText(UI_10_FONT_ID, left, y, "Open http://192.168.4.1/");
    y += 45;
    char buf[64];
    snprintf(buf, sizeof(buf), "Clients: %d", WiFi.softAPgetStationNum());
    renderer.drawText(UI_10_FONT_ID, left, y, buf);
    y += 45;
    snprintf(buf, sizeof(buf), "Page views: %d", pageViews);
    renderer.drawText(UI_10_FONT_ID, left, y, buf);
    y += 45;
    snprintf(buf, sizeof(buf), "Synthetic POST tests: %d", testSubmissions);
    renderer.drawText(UI_10_FONT_ID, left, y, buf);
    y += 45;
    renderer.drawText(UI_10_FONT_ID, left, y, TEMPLATE_NAMES[selectedTemplate]);
    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2,
                              labels.btn3, labels.btn4);
  } else if (state == STOPPED) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2, "Laboratory portal stopped");
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 50, "Tap to return to templates");
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2, "Select LAB network name");
  }
  renderer.displayBuffer();
}
