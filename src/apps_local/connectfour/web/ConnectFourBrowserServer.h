#pragma once
#include <cstdint>
#include <string>
#ifndef SIMULATOR
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#endif
#include "ConnectFourBrowserAdapter.h"

namespace c4web {

// Dedicated HTTP/WS transport. Rules and capability authorization stay in
// Round and SeatSession, not in the HTTP handlers or rendered browser page.
class Server final {
 public:
  Server() = default;
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  bool begin();
  void stop();
  void loop();
  bool deviceDrop(uint8_t column);
  bool deviceRematch();
  bool running() const { return running_; }
  bool clientSeen() const { return clientSeen_; }
  bool takeChanged() { const bool value = changed_; changed_ = false; return value; }
  const Round& round() const { return round_; }
  const char* url() const { return url_.c_str(); }
  const char* ssid() const { return "CrossPlay-ConnectFour"; }
 private:
#ifndef SIMULATOR
  void sendSnapshot(uint8_t client);
  void publishSnapshot();
  void sendResume(uint8_t client);
  void receive(uint8_t client, const uint8_t* bytes, size_t length);
  void configureRoutes();
  void servePage();
  void releaseDevMode();
  WebServer http_{80};
  WebSocketsServer ws_{81};
  DNSServer dns_;
  bool routesConfigured_ = false;
#endif
  Round round_;
  bool devModePaused_ = false;
  bool running_ = false;
  bool clientSeen_ = false;
  bool changed_ = false;
  std::string url_;
};
}  // namespace c4web
