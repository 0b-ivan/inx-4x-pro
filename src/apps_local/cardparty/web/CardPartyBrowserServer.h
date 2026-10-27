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
#include "../CardPartySdPack.h"
#include "../CardPartySessions.h"

namespace cardparty {
namespace web {

#ifndef SIMULATOR
#if defined(ESP_PLATFORM)
// arduinoWebSockets writes block while the peer's TCP window is closed: the
// ESP32 client retries select() for up to ~10 s per write, and disconnect()
// writes a close frame first. One phone in airplane mode would freeze the X4
// loop for every other player. These two calls never block.
class SocketServer final : public WebSocketsServer {
 public:
  using WebSocketsServer::WebSocketsServer;
  // Room for a frame in the TCP send buffer right now.
  bool writable(uint8_t client);
  // Close without a close frame.
  void drop(uint8_t client);
};
#else
using SocketServer = WebSocketsServer;  // host fake models writable()/drop()
#endif
#endif

// Dedicated X4-hosted HTTP/WebSocket server. Not registered with the shelf
// until the e-ink activity passes integration and hardware validation.
class Server final {
 public:
  enum class Mode : uint8_t { ExistingWifi, Hotspot };
  Server(Game& game, Sessions& sessions, const Deck& deck) : game_(game), sessions_(sessions), deck_(deck) {}
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
  bool hostRematch();
  bool running() const { return running_; }

  // Field telemetry for the hardware acceptance run. Contains counts only:
  // never tickets, names or cards.
  struct Diagnostics {
    bool valid = false;
    uint32_t freeHeap = 0, minFreeHeap = 0, maxAllocHeap = 0, freePsram = 0;
    uint8_t sockets = 0, stations = 0, seated = 0, connected = 0, resetReason = 0;
    uint16_t rejected = 0, takeovers = 0, idleClosed = 0, sendFailures = 0;
    uint16_t deferred = 0, dropped = 0;
  };
  Diagnostics diagnostics() const;
  // Unauthenticated sockets are closed after this, freeing a bounded slot.
  static constexpr uint32_t kAuthGraceMs = 20000;
  static constexpr uint32_t kDiagLogMs = 15000;
  // A pending answerer who drops keeps the round open this long.
  static constexpr uint32_t kTurnGraceMs = 30000;
  // Liveness: writable sockets are pinged; a socket that misses this many
  // pings in a row, or stays unwritable this long, is dropped.
  static constexpr uint32_t kPingMs = 5000;
  static constexpr uint8_t kMissedPings = 2;
  static constexpr uint32_t kStallDropMs = 15000;
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
  void sendError(uint8_t client, const char* reason = "REQUEST REJECTED");
  void closeSocket(uint8_t client);
  void flushPending();
  void heartbeat(uint32_t now);
  void closeIdleSockets(uint32_t now);
  void releaseExpiredTurns(uint32_t now);
  void logDiagnostics(uint32_t now);
  void fillSecret(uint8_t secret[kTicketBytes]);
  void releaseDevMode();
  WebServer http_{80};
  SocketServer ws_{81};
  DNSServer dns_;
  bool routesConfigured_ = false, dnsRunning_ = false, devModePaused_ = false;
  // Printable SD text and names can double under JSON escaping. Include all
  // plays, the largest private hand, winner text, prompt and fixed wire fields.
  static constexpr size_t kMessageBytes =
      2048 + 2 * (sdpack::kMaxPromptBytes + (kPileMax * kPickMax + kHandMax + kPickMax) * sdpack::kMaxAnswerBytes +
                  (kPlayers + 2) * 23);
  char message_[kMessageBytes] = {};
  SessionTicket tickets_[kPlayers] = {};
  uint32_t socket_[kPlayers] = {};
  bool liveSocket_[256] = {};  // WebSocket client slot must be connected
  static constexpr uint8_t kTrackedSlots = 16;
  uint32_t connectedAt_[kTrackedSlots] = {};
  uint32_t lastDiagLog_ = 0;
  uint32_t heldSince_[kPlayers] = {};
  // State frames are full snapshots: a seat whose socket is not writable
  // keeps one pending frame (2 = welcome with token) and gets the latest
  // state once it drains.
  uint8_t pending_[kPlayers] = {};
  uint8_t unanswered_[kTrackedSlots] = {};
  uint32_t stalledSince_[kTrackedSlots] = {};
  uint32_t lastPing_ = 0;
  uint16_t rejected_ = 0, takeovers_ = 0, idleClosed_ = 0, sendFailures_ = 0;
  uint16_t deferred_ = 0, dropped_ = 0;
#endif
  Game& game_;
  Sessions& sessions_;
  const Deck& deck_;
  Mode mode_ = Mode::ExistingWifi;
  bool running_ = false;
  std::string ssid_, ip_, url_;
  char password_[17] = {};
};

}  // namespace web
}  // namespace cardparty
