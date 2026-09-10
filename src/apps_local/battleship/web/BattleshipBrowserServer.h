#pragma once

#include <cstdint>
#include <string>

#ifndef SIMULATOR
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#endif

#include "BrowserCommands.h"
#include "BrowserSnapshot.h"

namespace bshipweb {

// Dedicated Battleship transport; only explicit command and view DTOs cross here.
class Server final {
 public:
  enum class NetworkMode : uint8_t { ExistingWifi, Hotspot };

  Server() = default;
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // ExistingWifi expects an already-connected STA. Hotspot creates the same
  // simple open-network shape the reader's file-transfer screen already uses.
  bool begin(NetworkMode mode);
  void stop();
  void loop();
  void publish(const BrowserSnapshot& snapshot);

  void setCommands(void* context, bool (*apply)(void*, const Command&, PlacementView&), void (*disconnect)(void*)) {
    context_ = context;
    apply_ = apply;
    disconnect_ = disconnect;
  }

  bool running() const { return running_; }
  bool hotspot() const { return mode_ == NetworkMode::Hotspot; }
  bool clientSeen() const { return clientSeen_; }
  const char* ssid() const { return ssid_.c_str(); }
  const char* ip() const { return ip_.c_str(); }
  const char* url() const { return url_.c_str(); }

 private:
#ifndef SIMULATOR
  void configureRoutes();
  void serveGamePage();
  void handleNotFound();
  bool startHotspot();
  bool startExistingWifi();
  bool startMdns();
  void releaseDevMode();

  void sendSnapshot(uint8_t client);
  void receive(uint8_t client, const uint8_t* payload, size_t size);
  void rotateToken();
  char token_[33] = {};
  int owner_ = -1;
  uint32_t lastCommandMs_ = 0;
  bool commandSeen_ = false;
  // Fixed member buffer avoids growing the activity-loop stack or per-message heap.
  char reply_[256] = {};

  WebSocketsServer ws_{81};
  WebServer http_{80};
  DNSServer dns_;
  bool routesConfigured_ = false;
  bool dnsRunning_ = false;
  // The server itself takes a Developer Mode yield because it owns port 80 and,
  // in hotspot mode, the radio. Callers may also hold an outer yield; DevMode's
  // yieldDepth is intentionally nestable.
  bool devModePaused_ = false;
#endif

  void* context_ = nullptr;
  bool (*apply_)(void*, const Command&, PlacementView&) = nullptr;
  void (*disconnect_)(void*) = nullptr;
  BrowserSnapshot snapshot_;
  NetworkMode mode_ = NetworkMode::ExistingWifi;
  bool running_ = false;
  bool clientSeen_ = false;
  std::string ssid_;
  std::string ip_;
  std::string url_;
};

}  // namespace bshipweb
