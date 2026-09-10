#include "BattleshipBrowserServer.h"

#ifndef SIMULATOR

#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "../../../DevMode.h"
#include "BattleshipPageHtml.generated.h"

namespace bshipweb {
namespace {
constexpr const char* kApSsid = "CrossPlay-Battleship";
constexpr const char* kMdnsHost = "crossplay";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 2;
constexpr uint16_t kDnsPort = 53;
constexpr const char* kGamePath = "/battleship";
}  // namespace

Server::~Server() { stop(); }

bool Server::begin(const NetworkMode mode) {
  if (running_) return true;

  owner_ = -1;
  commandSeen_ = false;
  rotateToken();
  rotateResumeToken();
  mode_ = mode;
  clientSeen_ = false;
  ssid_.clear();
  ip_.clear();
  url_.clear();

  if (!devModePaused_) {
    devmode::pause();
    devModePaused_ = true;
  }

  const bool networkReady = mode == NetworkMode::Hotspot ? startHotspot() : startExistingWifi();
  if (!networkReady) {
    releaseDevMode();
    return false;
  }

  if (!startMdns()) LOG_DBG("BSHIPWEB", "mDNS unavailable; IP fallback remains usable");

  configureRoutes();
  http_.begin();
  ws_.begin();
  ws_.enableHeartbeat(5000, 3000, 2);
  ws_.onEvent([this](uint8_t client, WStype_t type, uint8_t* payload, size_t size) {
    if (type == WStype_CONNECTED) {
      clientSeen_ = true;
      snapshot_.connected = true;
      sendSnapshot(client);
      sendOpponentName(client);
    } else if (type == WStype_DISCONNECTED) {
      clientSeen_ = ws_.connectedClients() != 0;
      if (owner_ == client) {
        owner_ = -1;
        commandSeen_ = false;
        rotateToken();
        if (disconnect_) disconnect_(context_);
      }
    } else if (type == WStype_TEXT) {
      receive(client, payload, size);
    }
  });
  running_ = true;

  LOG_INF("BSHIPWEB", "Browser server ready at %s (%s)", url_.c_str(), ip_.c_str());
  return true;
}

void Server::releaseDevMode() {
  if (!devModePaused_) return;
  devmode::resume();
  devModePaused_ = false;
}

void Server::stop() {
  owner_ = -1;
  token_[0] = 0;
  resumeToken_[0] = 0;
  const bool hadServer = running_ || dnsRunning_;
  running_ = false;
  clientSeen_ = false;

  if (hadServer) {
    ws_.close();
    http_.stop();
  }
  snapshot_ = {};

  if (dnsRunning_) {
    dns_.stop();
    dnsRunning_ = false;
  }

  if (hadServer) MDNS.end();

  if (mode_ == NetworkMode::Hotspot && hadServer) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  releaseDevMode();
  if (hadServer) LOG_DBG("BSHIPWEB", "Browser server stopped");
}

void Server::loop() {
  if (!running_) return;
  if (dnsRunning_) dns_.processNextRequest();
  http_.handleClient();
  ws_.loop();
}

void Server::sendSnapshot(const uint8_t client) {
  char message[128];
  size_t size = serializeSnapshot(snapshot_, message, sizeof(message));
  if (size) ws_.sendTXT(client, message, size);
  size = serializeFleetStatus(snapshot_, message, sizeof(message));
  if (size) ws_.sendTXT(client, message, size);
}

void Server::sendOpponentName(const uint8_t client) {
  // player::name() currently guarantees a short generated name made from known
  // uppercase words and spaces, so it is safe to place directly in this JSON
  // string. The future typed-name PlayerService should own escaping/validation.
  const int n = snprintf(reply_, sizeof(reply_), "{\"type\":\"peer\",\"name\":\"%s\"}", opponentName_.c_str());
  if (n > 0 && static_cast<size_t>(n) < sizeof(reply_)) ws_.sendTXT(client, reply_, static_cast<size_t>(n));
}

void Server::sendResumeCapability(const uint8_t client) {
  if (!resumeToken_[0]) return;
  const int n = snprintf(reply_, sizeof(reply_), "{\"type\":\"resume\",\"token\":\"%s\"}", resumeToken_);
  if (n > 0 && static_cast<size_t>(n) < sizeof(reply_)) ws_.sendTXT(client, reply_, static_cast<size_t>(n));
}

void Server::publish(const BrowserSnapshot& snapshot) {
  if (sameSnapshot(snapshot_, snapshot)) return;
  snapshot_ = snapshot;
  if (!running_) return;
  char message[128];
  size_t size = serializeSnapshot(snapshot_, message, sizeof(message));
  if (size) ws_.broadcastTXT(message, size);
  size = serializeFleetStatus(snapshot_, message, sizeof(message));
  if (size) ws_.broadcastTXT(message, size);
}

void Server::publishPlacement(const PlacementView& view) {
  if (!running_ || owner_ < 0) return;
  const size_t n = serializePlacement(view, true, reply_, sizeof(reply_));
  if (n) ws_.sendTXT(static_cast<uint8_t>(owner_), reply_, n);
}

void Server::rotateToken() {
  for (int i = 0; i < 4; ++i) snprintf(token_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
}

void Server::rotateResumeToken() {
  for (int i = 0; i < 4; ++i) snprintf(resumeToken_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
}

void Server::receive(uint8_t client, const uint8_t* payload, size_t size) {
  Command command;
  if (!running_ || !apply_ || !parseCommand(payload, size, command) || strcmp(command.token, token_) ||
      (owner_ >= 0 && owner_ != client) ||
      (owner_ < 0 && command.kind != CommandKind::Profile && command.kind != CommandKind::Resume)) {
    char error[] = "{\"type\":\"error\"}";
    ws_.sendTXT(client, error, sizeof(error) - 1);
    return;
  }

  if (command.kind == CommandKind::Resume && strcmp(command.resumeToken, resumeToken_)) {
    char error[] = "{\"type\":\"error\"}";
    ws_.sendTXT(client, error, sizeof(error) - 1);
    return;
  }

  const uint32_t now = millis();
  if (commandSeen_ && static_cast<uint32_t>(now - lastCommandMs_) < 100) {
    char error[] = "{\"type\":\"error\"}";
    ws_.sendTXT(client, error, sizeof(error) - 1);
    return;
  }
  commandSeen_ = true;
  lastCommandMs_ = now;

  PlacementView view;
  const bool accepted = apply_(context_, command, view);
  if (owner_ < 0) {
    if (!accepted) {
      char error[] = "{\"type\":\"error\"}";
      ws_.sendTXT(client, error, sizeof(error) - 1);
      return;
    }
    owner_ = client;
    if (command.kind == CommandKind::Profile) rotateResumeToken();
  }

  const size_t n = serializePlacement(view, accepted, reply_, sizeof(reply_));
  if (n) ws_.sendTXT(client, reply_, n);
  if (accepted && (command.kind == CommandKind::Profile || command.kind == CommandKind::Resume)) sendResumeCapability(client);
  if (command.kind == CommandKind::Resume) sendSnapshot(client);
}

void Server::configureRoutes() {
  if (routesConfigured_) return;

  http_.on("/", HTTP_GET, [this] { serveGamePage(); });
  http_.on(kGamePath, HTTP_GET, [this] { serveGamePage(); });
  http_.on("/battleship/session", HTTP_GET, [this] {
    http_.sendHeader("Cache-Control", "no-store");
    http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
    http_.send(200, "text/plain", token_);
  });
  http_.onNotFound([this] { handleNotFound(); });
  routesConfigured_ = true;
}

void Server::serveGamePage() {
  http_.sendHeader("Content-Encoding", "gzip");
  http_.sendHeader("Cache-Control", "no-store");
  http_.sendHeader("X-Frame-Options", "DENY");
  http_.send_P(200, "text/html", BattleshipPageHtml, BattleshipPageHtmlCompressedSize);
}

void Server::handleNotFound() {
  if (mode_ == NetworkMode::Hotspot && http_.method() == HTTP_GET) {
    serveGamePage();
    return;
  }
  http_.send(404, "text/plain", "Not found");
}

bool Server::startHotspot() {
  WiFi.mode(WIFI_AP);
  delay(50);

  if (!WiFi.softAP(kApSsid, nullptr, kApChannel, false, kApMaxClients)) {
    LOG_ERR("BSHIPWEB", "Failed to start Battleship hotspot");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  const IPAddress address = WiFi.softAPIP();
  ssid_ = kApSsid;
  ip_ = address.toString().c_str();
  url_ = std::string("http://") + kMdnsHost + ".local" + kGamePath;

  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(kDnsPort, "*", address);
  dnsRunning_ = true;
  return true;
}

bool Server::startExistingWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_DBG("BSHIPWEB", "Existing Wi-Fi requested without an active STA connection");
    return false;
  }

  ssid_ = WiFi.SSID().c_str();
  ip_ = WiFi.localIP().toString().c_str();
  url_ = std::string("http://") + kMdnsHost + ".local" + kGamePath;
  return true;
}

bool Server::startMdns() {
  MDNS.end();
  if (!MDNS.begin(kMdnsHost)) return false;
  MDNS.addService("http", "tcp", 80);
  return true;
}

}  // namespace bshipweb

#else

namespace bshipweb {

Server::~Server() = default;

bool Server::begin(const NetworkMode mode) {
  mode_ = mode;
  running_ = false;
  clientSeen_ = false;
  ssid_.clear();
  ip_.clear();
  url_.clear();
  return false;
}

void Server::stop() {
  running_ = false;
  clientSeen_ = false;
}

void Server::loop() {}
void Server::publish(const BrowserSnapshot& snapshot) { snapshot_ = snapshot; }
void Server::publishPlacement(const PlacementView&) {}

}  // namespace bshipweb

#endif
