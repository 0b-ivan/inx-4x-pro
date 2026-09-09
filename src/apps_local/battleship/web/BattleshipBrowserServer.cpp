#include "BattleshipBrowserServer.h"

#ifndef SIMULATOR

#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>

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

  mode_ = mode;
  clientSeen_ = false;
  ssid_.clear();
  ip_.clear();
  url_.clear();

  // Port 80 and, for Hotspot, the radio are mutually exclusive with Developer
  // Mode. Own the yield here rather than relying on a particular activity to
  // remember it; DevMode uses a depth counter, so an outer owner can nest it.
  if (!devModePaused_) {
    devmode::pause();
    devModePaused_ = true;
  }

  const bool networkReady = mode == NetworkMode::Hotspot ? startHotspot() : startExistingWifi();
  if (!networkReady) {
    releaseDevMode();
    return false;
  }

  if (!startMdns()) {
    LOG_DBG("BSHIPWEB", "mDNS unavailable; IP fallback remains usable");
  }

  configureRoutes();
  http_.begin();
  ws_.begin();
  ws_.enableHeartbeat(5000, 3000, 2);
  ws_.onEvent([this](uint8_t client, WStype_t type, uint8_t*, size_t) {
    // Text, binary and fragmented application messages never mutate the game.
    if (type == WStype_CONNECTED) {
      clientSeen_ = true;
      snapshot_.connected = true;
      sendSnapshot(client);
    } else if (type == WStype_DISCONNECTED) {
      clientSeen_ = ws_.connectedClients() != 0;
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
  const size_t size = serializeSnapshot(snapshot_, message, sizeof(message));
  if (size) ws_.sendTXT(client, message, size);
}

void Server::publish(const BrowserSnapshot& snapshot) {
  if (sameSnapshot(snapshot_, snapshot)) return;
  snapshot_ = snapshot;
  if (!running_) return;
  char message[128];
  const size_t size = serializeSnapshot(snapshot_, message, sizeof(message));
  if (size) ws_.broadcastTXT(message, size);
}

void Server::configureRoutes() {
  if (routesConfigured_) return;

  http_.on("/", HTTP_GET, [this] { serveGamePage(); });
  http_.on(kGamePath, HTTP_GET, [this] { serveGamePage(); });
  http_.onNotFound([this] { handleNotFound(); });
  routesConfigured_ = true;
}

void Server::serveGamePage() {
  http_.sendHeader("Content-Encoding", "gzip");
  http_.send_P(200, "text/html", BattleshipPageHtml, BattleshipPageHtmlCompressedSize);
}

void Server::handleNotFound() {
  // In AP mode every hostname resolves to us. Serving the game shell for GET
  // requests gives phones a useful captive-portal landing page instead of a
  // dead 404 when their connectivity probe opens automatically.
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

}  // namespace bshipweb

#endif
