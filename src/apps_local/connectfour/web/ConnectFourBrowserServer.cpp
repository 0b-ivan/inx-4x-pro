#include "ConnectFourBrowserServer.h"

#ifndef SIMULATOR
#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_system.h>
#include "../../../DevMode.h"
#include <cstdio>
#include "ConnectFourBrowserPageHtml.generated.h"

namespace c4web {
namespace {
uint32_t randomWord() { return esp_random(); }
}

Server::~Server() { stop(); }

bool Server::begin() {
  if (running_) return true;
  clientSeen_ = false;
  changed_ = true;
  url_.clear();
  if (!devModePaused_) { devmode::pause(); devModePaused_ = true; }
  WiFi.mode(WIFI_AP);
  delay(50);
  // Open short-range game hotspot; seat capabilities still gate moves.
  if (!WiFi.softAP(ssid(), nullptr, 1, false, 2)) {
    WiFi.mode(WIFI_OFF);
      devmode::resume(); devModePaused_ = false;
    return false;
  }
  // ESP32 RF provides entropy after AP startup, including the seat secrets.
  round_.begin(&randomWord);
  url_ = std::string("http://") + WiFi.softAPIP().toString().c_str() + "/connectfour";
  MDNS.end();
  if (MDNS.begin("crossplay")) MDNS.addService("http", "tcp", 80);
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  configureRoutes();
  http_.begin();
  ws_.begin();
  ws_.enableHeartbeat(5000,3000,2);
  ws_.onEvent([this](uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
      clientSeen_ = true; changed_ = true;
      sendSnapshot(client);
    } else if (type == WStype_DISCONNECTED) {
      clientSeen_ = ws_.connectedClients() != 0;
      round_.disconnected(client, &randomWord);
      changed_ = true;
    } else if (type == WStype_TEXT) {
      receive(client, payload, length);
    }
  });
  running_ = true;
  LOG_INF("C4WEB", "Connect Four browser at %s", url_.c_str());
  return true;
}
void Server::stop() {
  const bool wasRunning = running_;
  running_ = false;
  clientSeen_ = false;
  round_.stop();
  if (wasRunning) {
    ws_.close();
    http_.stop();
    dns_.stop();
    MDNS.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  url_.clear();
  if (devModePaused_) { devmode::resume(); devModePaused_ = false; }
  changed_ = true;
}
void Server::loop() {
  if (!running_) return;
  dns_.processNextRequest();
  http_.handleClient();
  ws_.loop();
}
void Server::sendSnapshot(uint8_t client) {
  char buffer[192]{};
  const size_t size = round_.snapshot(buffer, sizeof(buffer));
  if (size) ws_.sendTXT(client, buffer, size);
}
void Server::publishSnapshot() {
  if (!running_) return;
  char buffer[192]{};
  const size_t size = round_.snapshot(buffer, sizeof(buffer));
  if (size) ws_.broadcastTXT(buffer, size);
  changed_ = true;
}
void Server::sendResume(uint8_t client) {
  if (!round_.hasOwner() || round_.owner() != client) return;
  char buffer[96]{};
  const int n = std::snprintf(buffer, sizeof(buffer),
                            "{\"type\":\"resume\",\"token\":\"%s\"}", round_.resumeToken());
  if (n > 0 && static_cast<size_t>(n) < sizeof(buffer)) ws_.sendTXT(client, buffer, static_cast<size_t>(n));
}
void Server::receive(uint8_t client, const uint8_t* bytes, size_t length) {
  Command command;
  if (!parseCommand(bytes, length, command) ||
      !round_.receive(client, bytes, length, millis(), &randomWord)) {
    char error[]="{\"type\":\"error\"}";
    ws_.sendTXT(client, error, sizeof(error)-1);
    return;
  }
  if (command.kind == Kind::Join || command.kind == Kind::Resume) sendResume(client);
  publishSnapshot();
}
void Server::configureRoutes() {
  if (routesConfigured_) return;
  http_.on("/", HTTP_GET, [this]{ servePage(); });
  http_.on("/connectfour", HTTP_GET, [this]{ servePage(); });
  http_.on("/connectfour/session", HTTP_GET, [this]{
    http_.sendHeader("Cache-Control", "no-store");
    http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
    http_.send(200, "text/plain", round_.token());
  });
  http_.onNotFound([this]{ http_.send(404, "text/plain", "Not found"); });
  routesConfigured_ = true;
}
void Server::servePage() {
  http_.sendHeader("Content-Encoding", "gzip");
  http_.sendHeader("Cache-Control", "no-store");
  http_.sendHeader("X-Frame-Options", "DENY");
  http_.send_P(200, "text/html", ConnectFourBrowserPageHtml, ConnectFourBrowserPageHtmlCompressedSize);
}
bool Server::deviceDrop(uint8_t column) {
  if (!running_ || !round_.deviceDrop(column)) return false;
  publishSnapshot();
  return true;
}
bool Server::deviceRematch() {
  if (!running_ || !round_.deviceRematch()) return false;
  publishSnapshot();
  return true;
}
}  // namespace c4web
#else
namespace c4web {
Server::~Server() = default;
bool Server::begin() { return false; }
void Server::stop() { running_ = false; round_.stop(); }
void Server::loop() {}
bool Server::deviceDrop(uint8_t) { return false; }
bool Server::deviceRematch() { return false; }
}
#endif
