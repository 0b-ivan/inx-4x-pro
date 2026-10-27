#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#ifndef SIMULATOR
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#endif
#include "../CardPartyCore.h"
#include "../CardPartySessions.h"

namespace cardparty { namespace web {

// Dedicated X4-hosted HTTP/WebSocket server. Not registered with the shelf
// until the e-ink activity passes integration and hardware validation.
class Server final {
 public:
  enum class Mode : uint8_t { ExistingWifi, Hotspot };
  Server(Game& game, Sessions& sessions, const Deck& deck)
    : game_(game), sessions_(sessions), deck_(deck) {}
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  bool begin(Mode mode);
  void loop();
  void stop();
  // These calls are for the physical X4 only. No wire commands dispatch them.
  bool hostStart();
  bool hostNext();
  bool hostAbort();
  bool hostClose();
  bool running() const { return running_; }
  bool hotspot() const { return mode_ == Mode::Hotspot; }
  const char* ssid() const { return ssid_.c_str(); }
  const char* password() const { return password_; }
  const char* ip() const { return ip_.c_str(); }
  const char* url() const { return url_.c_str(); }

 private:
#ifndef SIMULATOR
  void configureRoutes();
  bool startWifi();
  bool startHotspot();
  void sendGamePage();
  void onMessage(uint8_t client, const uint8_t* payload, size_t size);
  void onDisconnect(uint8_t client);
  void sendState(uint8_t seat, bool welcome);
  void publishAll();
  void sendError(uint8_t client);
  void fillSecret(uint8_t secret[kTicketBytes]);
  void endRadio();
  WebServer http_{80};
  WebSocketsServer ws_{81};
  DNSServer dns_;
  bool routesConfigured_=false, dnsRunning_=false, devModePaused_=false;
  uint8_t passwordIndex_=0;
  char message_[6144]={};
  SessionTicket tickets_[kPlayers]={};
  uint32_t socket_[kPlayers]={};
  bool liveSocket_[256]={}; // WebSocket client slot must be connected
#endif
  Game& game_;
  Sessions& sessions_;
  const Deck& deck_;
  Mode mode_=Mode::ExistingWifi;
  bool running_=false;
  std::string ssid_, ip_,url_;
  char password_[17]={};
};

}} // namespace cardparty::web
