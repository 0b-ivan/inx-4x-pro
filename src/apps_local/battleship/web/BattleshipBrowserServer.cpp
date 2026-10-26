#include "BattleshipBrowserServer.h"

#ifndef SIMULATOR

#include <ESPmDNS.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstdio>
#include <cstring>

#include "../../../DevMode.h"
#include "../../player/PlayerWebApi.h"
#include "BattleshipPageHtml.generated.h"

namespace bshipweb {
namespace {
constexpr const char* kApSsid = "CrossPlay-Battleship";
constexpr const char* kMdnsHost = "crossplay";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxClients = 2;
constexpr uint16_t kDnsPort = 53;
constexpr const char* kGamePath = "/battleship";
uint32_t randomWord() { return esp_random(); }

// The bootstrap session token is public to clients on the LAN. Never use it
// alone to read or mutate the X4's stored player identities. The separate
// 128-bit seat secret is only sent to the accepted WebSocket owner.
bool playerAuthorized(WebServer& http, const webplay::SeatSession& seat) {
  return http.hasArg("token") && http.hasArg("owner") &&
         seat.authorized(http.arg("token").c_str(), http.arg("owner").c_str());
}

void sendPlayerReply(WebServer& http, const playerweb::Reply& reply) {
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  http.send(reply.status, "application/json", reply.body.c_str());
}

size_t serializePeerName(const std::string& name, char* output, const size_t capacity) {
  if (output == nullptr || capacity < 28) return 0;
  size_t at = 0;
  const char prefix[] = "{\"type\":\"peer\",\"name\":\"";
  const char suffix[] = "\"}";
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i) output[at++] = prefix[i];
  for (const unsigned char ch : name) {
    if (ch < 0x20) continue;
    if (ch == '"' || ch == '\\') {
      if (at + 2 + sizeof(suffix) > capacity) return 0;
      output[at++] = '\\';
      output[at++] = static_cast<char>(ch);
    } else {
      if (at + 1 + sizeof(suffix) > capacity) return 0;
      output[at++] = static_cast<char>(ch);
    }
  }
  if (at + sizeof(suffix) > capacity) return 0;
  for (size_t i = 0; i < sizeof(suffix) - 1; ++i) output[at++] = suffix[i];
  output[at] = '\0';
  return at;
}
}  // namespace

Server::~Server() { stop(); }

bool Server::begin(const NetworkMode mode) {
  if (running_) return true;

  opponentName_ = playerweb::displayName();
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

  // ESP32 RF has started in AP/STA mode; initialize seat capabilities only
  // after it can contribute fresh entropy to esp_random().
  seat_.begin(&randomWord);

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
      // Never disclose the device's active Player identity to unclaimed peers.
    } else if (type == WStype_DISCONNECTED) {
      clientSeen_ = ws_.connectedClients() != 0;
      if (seat_.disconnect(client, &randomWord) && disconnect_) disconnect_(context_);
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
  seat_.stop();
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
  // Escaped, validated JSON sent only to the owner; spectators cannot use
  // public game-state frames to enumerate the X4's active Player identity.
  const size_t n = serializePeerName(opponentName_, reply_, sizeof(reply_));
  if (n != 0) ws_.sendTXT(client, reply_, n);
}

void Server::sendResumeCapability(const uint8_t client) {
  if (!seat_.resumeToken()[0]) return;
  const int n = snprintf(reply_, sizeof(reply_), "{\"type\":\"resume\",\"token\":\"%s\"}", seat_.resumeToken());
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
  if (!running_ || !seat_.hasOwner()) return;
  const size_t n = serializePlacement(view, true, reply_, sizeof(reply_));
  if (n) ws_.sendTXT(seat_.owner(), reply_, n);
}

void Server::receive(uint8_t client, const uint8_t* payload, size_t size) {
  Command command;
  if (!running_ || !apply_ || !parseCommand(payload, size, command) ||
      !seat_.admit(client, command.token, command.kind == CommandKind::Profile,
                   command.kind == CommandKind::Resume, command.resumeToken, millis())) {
    char error[] = "{\"type\":\"error\"}";
    ws_.sendTXT(client, error, sizeof(error) - 1);
    return;
  }

  PlacementView view;
  const bool accepted = apply_(context_, command, view);
  if (!seat_.hasOwner()) {
    if (!accepted) {
      char error[] = "{\"type\":\"error\"}";
      ws_.sendTXT(client, error, sizeof(error) - 1);
      return;
    }
    seat_.claim(client, command.kind == CommandKind::Profile, &randomWord);
  }

  if (accepted && command.kind == CommandKind::Resume)
    seat_.rebind(client, command.resumeToken);

  const size_t n = serializePlacement(view, accepted, reply_, sizeof(reply_));
  if (n) ws_.sendTXT(client, reply_, n);
  if (accepted && (command.kind == CommandKind::Profile || command.kind == CommandKind::Resume)) {
    sendOpponentName(client);
    sendResumeCapability(client);
  }
  if (command.kind == CommandKind::Resume) sendSnapshot(client);
}

void Server::configureRoutes() {
  if (routesConfigured_) return;

  http_.on("/", HTTP_GET, [this] { serveGamePage(); });
  http_.on(kGamePath, HTTP_GET, [this] { serveGamePage(); });
  http_.on("/battleship/session", HTTP_GET, [this] {
    http_.sendHeader("Cache-Control", "no-store");
    http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
    http_.send(200, "text/plain", seat_.token());
  });
  http_.on("/player/state", HTTP_GET, [this] {
    if (!playerAuthorized(http_, seat_)) {
      sendPlayerReply(http_, playerweb::Reply{403, "{\"ok\":false,\"message\":\"FORBIDDEN\"}"});
      return;
    }
    sendPlayerReply(http_, playerweb::state());
  });
  http_.on("/player/action", HTTP_POST, [this] {
    if (!playerAuthorized(http_, seat_)) {
      sendPlayerReply(http_, playerweb::Reply{403, "{\"ok\":false,\"message\":\"FORBIDDEN\"}"});
      return;
    }
    if (snapshot_.phase == BrowserPhase::Playing) {
      sendPlayerReply(http_, playerweb::Reply{409, "{\"ok\":false,\"message\":\"PLAYER LOCKED DURING MATCH\"}"});
      return;
    }

    const String action = http_.arg("action");
    playerweb::Reply reply;
    if (action == "guest") {
      reply = playerweb::useGuest();
    } else if (action == "login") {
      reply = playerweb::login(http_.arg("id").c_str(), http_.arg("pin").c_str());
    } else if (action == "register") {
      reply = playerweb::registerGuest(http_.arg("name").c_str(), http_.arg("pin").c_str());
    } else if (action == "callsign") {
      reply = playerweb::stepGuestCallsign(http_.arg("slot").toInt());
    } else {
      reply = playerweb::Reply{400, "{\"ok\":false,\"message\":\"UNKNOWN ACTION\"}"};
    }

    if (reply.status == 200) {
      opponentName_ = playerweb::displayName();
      char peerMessage[96]{};
      const size_t n = serializePeerName(opponentName_, peerMessage, sizeof(peerMessage));
      if (n != 0 && seat_.hasOwner()) ws_.sendTXT(seat_.owner(), peerMessage, n);
    }
    sendPlayerReply(http_, reply);
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

  // Intentionally open local game AP: joining requires no WPA password.
  // Browser seat ownership and private resume capabilities remain enforced.
  if (!WiFi.softAP(kApSsid, nullptr, kApChannel, false, kApMaxClients)) {
    LOG_ERR("BSHIPWEB", "Failed to start Battleship hotspot");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  const IPAddress address = WiFi.softAPIP();
  ssid_ = kApSsid;
  ip_ = address.toString().c_str();
  // QR links use the numeric address because captive-network browsers may not resolve mDNS.
  url_ = std::string("http://") + ip_ + kGamePath;

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
  // QR links use the numeric address because captive-network browsers may not resolve mDNS.
  url_ = std::string("http://") + ip_ + kGamePath;
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
