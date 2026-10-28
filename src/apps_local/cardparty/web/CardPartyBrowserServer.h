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
#include "../CardPartyHistory.h"
#include "../CardPartySdPack.h"
#include "../CardPartySessions.h"
#include "../CardPartyTable.h"

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

  // The host supplies the language, leaving this transport firmware-independent.
  void setLanguage(const char* code) {
    const bool de = code && code[0] == 'd' && code[1] == 'e' && code[2] == '\0';
    language_[0] = de ? 'd' : 'e';
    language_[1] = de ? 'e' : 'n';
  }
  void setHistory(history::Book* book) { history_ = book; }
  bool begin(Mode mode);
  void loop();
  void stop();
  // These calls are for the physical X4 only. No wire commands dispatch them.
  bool hostStart();
  bool hostNext();
  bool hostAbort();
  bool hostClose();
  bool hostRematch();
  bool hostPause();
  bool hostUnpause();
  bool hostAddTime();
  // Answer to a block: go on without the missing player.
  bool hostContinue();
  bool hostKick(uint8_t seat);
  bool hostEnd();
  void hostSettings(const Settings& settings);
  // The Deck behind deck_ holds a new pack: lobby, or a fresh deal between rounds.
  bool hostDeckChanged();
  // Category switches changed: tell the phones nothing moved but the counts.
  void hostRefresh();
  const Table& table() const { return table_; }
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
  const char* seatLabel(uint8_t seat, char* target, size_t capacity) const;

 private:
#ifndef SIMULATOR
  void configureRoutes();
  bool startWifi();
  bool startHotspot();
  void sendGamePage();
  void onMessage(uint8_t client, const uint8_t* payload, size_t size);
  void onDisconnect(uint8_t client);
  void sendState(uint8_t seat, bool welcome);
  void captureHistory();
  void publishAll();
  void sendError(uint8_t client, const char* code = "rejected");
  void closeSocket(uint8_t client);
  void flushPending();
  void heartbeat(uint32_t now);
  void closeIdleSockets(uint32_t now);
  void tickTable();
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
                  (kPlayers + 2) * (23 + 24) + kPickMax * sdpack::kMaxAnswerBytes);
  char message_[kMessageBytes] = {};
  SessionTicket tickets_[kPlayers] = {};
  uint32_t socket_[kPlayers] = {};
  bool liveSocket_[256] = {};  // WebSocket client slot must be connected
  static constexpr uint8_t kTrackedSlots = 16;
  uint32_t connectedAt_[kTrackedSlots] = {};
  uint32_t lastDiagLog_ = 0;
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
  Table table_{game_};
  Mode mode_ = Mode::ExistingWifi;
  bool running_ = false;
  std::string ssid_, ip_, url_;
  char password_[17] = {};
  char language_[3] = "en";
  history::Book* history_ = nullptr;  // provided by X4; transport does not own storage
  uint64_t playerIds_[kPlayers] = {};
  uint8_t playerWords_[kPlayers] = {};
  static const char* word(uint8_t id) {
    static constexpr const char* words[64] = {
      "Toast","Keks","Komet","Fuchs","Mond","Igel","Kompass","Funke",
      "Birne","Roboter","Kabel","Wolke","Wuerfel","Maus","Tasse","Pixel",
      "Drache","Tinte","Pfeil","Kiwi","Kaktus","Nebel","Wal","Stern",
      "Pilz","Dachs","Quarz","Echo","Trommel","Falter","Buch","Knopf",
      "Feder","Kreisel","Sonne","Rabe","Blitz","Uhr","Kran","Insel",
      "Nuss","Luchs","Radio","Bohne","Kiesel","Baum","Hut","Ninja",
      "Karte","Moewe","Fels","Leuchtturm","Panda","Ritter","Dingo","Mohn",
      "Zebra","Lava","Otter","Segel","Tango","Tiger","Hase","Lotus"
    };
    return words[id & 63u];
  }
  void assignId(uint8_t seat, uint64_t id);
  uint32_t identitySequence_ = 0;
  uint16_t recordedRound_ = 0;
  bool recordedFinal_ = false;
};

}  // namespace web
}  // namespace cardparty
