#include "CardPartyBrowserServer.h"

#include "CardPartyProtocol.h"

#ifndef SIMULATOR

#include <WiFi.h>
#include <esp_system.h>
#if defined(ESP_PLATFORM)
#include <sys/select.h>
#endif
#include <Logging.h>

#include <cstring>

#include "../../../DevMode.h"
#include "CardPartyPageHtml.generated.h"

namespace cardparty {
namespace web {
namespace {
constexpr const char* kSsid = "CrossPlay-CAW";
constexpr const char* kPath = "/cardparty";
// Eight players, plus limited station headroom for reconnect handshakes.
// The Game and Sessions classes still enforce the strict eight-seat limit.
constexpr uint8_t kMaxClients = 10;
constexpr uint16_t kDnsPort = 53;
const char* phaseName(Phase phase) {
  switch (phase) {
    case Phase::Lobby:
      return "Lobby";
    case Phase::Playing:
      return "Playing";
    case Phase::Judging:
      return "Judging";
    case Phase::Reveal:
      return "Reveal";
    case Phase::Finished:
      return "Finished";
  }
  return "Lobby";
}
}  // namespace

#if defined(ESP_PLATFORM)
bool SocketServer::writable(uint8_t client) {
  if (client >= WEBSOCKETS_SERVER_CLIENT_MAX) return false;
  WSclient_t& c = _clients[client];
  if (!c.tcp || !c.tcp->connected()) return false;
  const int fd = c.tcp->fd();
  if (fd < 0) return false;
  // lwIP reports writable while more than TCP_SNDLOWAT bytes are free
  // (2873 of 5744 here), more than a typical ~2 KB state frame.
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  timeval now{0, 0};
  return select(fd + 1, nullptr, &set, nullptr, &now) > 0 && FD_ISSET(fd, &set);
}
void SocketServer::drop(uint8_t client) {
  if (client >= WEBSOCKETS_SERVER_CLIENT_MAX || !_clients[client].tcp) return;
  clientDisconnect(&_clients[client]);  // fires WStype_DISCONNECTED
}
#endif

Server::~Server() { stop(); }

void Server::fillSecret(uint8_t secret[kTicketBytes]) {
  // Hardware RNG, independent from the deterministic card-shuffle PRNG.
  // Called after WiFi radio has been enabled so ESP32's entropy is available.
  for (size_t i = 0; i < kTicketBytes; i += 4) {
    const uint32_t word = esp_random();
    for (size_t j = 0; j < 4 && i + j < kTicketBytes; ++j) secret[i + j] = static_cast<uint8_t>(word >> (8 * j));
  }
}

bool Server::startHotspot() {
  WiFi.mode(WIFI_AP);
  delay(50);
  uint8_t entropy[kTicketBytes] = {};
  fillSecret(entropy);
  char hex[33] = {};
  encodeSecret(entropy, hex);
  std::memcpy(password_, hex, 16);
  password_[16] = 0;
  if (!WiFi.softAP(kSsid, password_, 1, false, kMaxClients)) {
    WiFi.mode(WIFI_OFF);
    password_[0] = 0;
    return false;
  }
  const IPAddress local = WiFi.softAPIP();
  ssid_ = kSsid;
  ip_ = local.toString().c_str();
  url_ = std::string("http://") + ip_ + kPath;
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(kDnsPort, "*", local);
  dnsRunning_ = true;
  return true;
}

bool Server::startWifi() {
  if (WiFi.status() != WL_CONNECTED) return false;
  password_[0] = 0;
  ssid_ = WiFi.SSID().c_str();
  ip_ = WiFi.localIP().toString().c_str();
  url_ = std::string("http://") + ip_ + kPath;
  return true;
}

bool Server::begin(Mode mode) {
  if (running_) return true;
  if (game_.phase() != Phase::Lobby) return false;
  mode_ = mode;
  for (uint8_t i = 0; i < kPlayers; ++i) {
    socket_[i] = kNoConnection;
    tickets_[i] = SessionTicket{};
  }
  sessions_.reset();
  table_.reset();
  for (bool& connected : liveSocket_) connected = false;
  for (uint8_t& p : pending_) p = 0;
  for (uint8_t i = 0; i < kTrackedSlots; ++i) {
    unanswered_[i] = 0;
    stalledSince_[i] = 0;
  }
  rejected_ = takeovers_ = idleClosed_ = sendFailures_ = deferred_ = dropped_ = 0;
  ssid_.clear();
  ip_.clear();
  url_.clear();
  password_[0] = 0;
  if (!devModePaused_) {
    devmode::pause();
    devModePaused_ = true;
  }
  const bool networkOk = mode == Mode::Hotspot ? startHotspot() : startWifi();
  if (!networkOk) {
    releaseDevMode();
    return false;
  }
  configureRoutes();
  http_.begin();
  // Reject cross-site WebSocket handshakes before issuing any player ticket.
  // Browsers set Origin for the document, i.e. HTTP port 80, not WS port 81.
  // A native client could forge Origin: this is cross-site protection, not
  // a replacement for per-seat tokens or transport encryption.
  static const char* kMandatoryHeaders[] = {"Origin"};
  ws_.onValidateHttpHeader(
      [this](String header, String value) {
        if (!header.equalsIgnoreCase("Origin")) return true;
        const std::string expected = std::string("http://") + ip_;
        return value == expected.c_str() || value == (expected + ":80").c_str();
      },
      kMandatoryHeaders, 1);
  ws_.begin();
  // No library heartbeat: its ping and its timeout close frame both block on
  // a stalled peer. heartbeat() pings only writable sockets.
  ws_.onEvent([this](uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
    if (client < kTrackedSlots && type != WStype_DISCONNECTED) {
      unanswered_[client] = 0;  // any frame from the browser proves liveness
      stalledSince_[client] = 0;
    }
    if (type == WStype_CONNECTED) {
      liveSocket_[client] = true;
      if (client < kTrackedSlots) connectedAt_[client] = millis();
      // Never block the event loop for a greeting to a full TCP window.
      char hello[48] = {};
      JsonWriter greeting(hello, sizeof(hello));
      greeting.raw("{\"type\":\"hello\",\"lang\":");
      greeting.quoted(language_);
      greeting.raw("}");
      if (greeting.ok() && ws_.writable(client)) ws_.sendTXT(client, hello, greeting.size());
    } else if (type == WStype_DISCONNECTED) {
      liveSocket_[client] = false;
      onDisconnect(client);
    } else if (type == WStype_TEXT && liveSocket_[client])
      onMessage(client, payload, length);
  });
  running_ = true;
  lastDiagLog_ = lastPing_ = millis();
  LOG_INF("CARDPARTY", "Game server ready at %s (reset reason %d)", url_.c_str(), static_cast<int>(esp_reset_reason()));
  return true;
}

void Server::releaseDevMode() {
  if (!devModePaused_) return;
  devmode::resume();
  devModePaused_ = false;
}

void Server::stop() {
  const bool active = running_ || dnsRunning_;
  running_ = false;
  if (active) {
    ws_.close();
    http_.stop();
  }
  if (dnsRunning_) {
    dns_.stop();
    dnsRunning_ = false;
  }
  if (active && mode_ == Mode::Hotspot) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  password_[0] = 0;
  for (uint8_t i = 0; i < kPlayers; ++i) {
    tickets_[i] = SessionTicket{};
    socket_[i] = kNoConnection;
    playerIds_[i] = 0;
    playerWords_[i] = 0;
    pending_[i] = 0;
  }
  sessions_.reset();
  for (bool& connected : liveSocket_) connected = false;
  releaseDevMode();
}

void Server::loop() {
  if (!running_) return;
  if (dnsRunning_) dns_.processNextRequest();
  http_.handleClient();
  ws_.loop();
  const uint32_t now = millis();
  flushPending();
  if (static_cast<uint32_t>(now - lastPing_) >= kPingMs) heartbeat(now);
  closeIdleSockets(now);
  tickTable();
  if (static_cast<uint32_t>(now - lastDiagLog_) >= kDiagLogMs) logDiagnostics(now);
}
void Server::closeSocket(uint8_t client) {
  // The close frame is a write too: a stalled peer is dropped without it.
  if (ws_.writable(client))
    ws_.disconnect(client);
  else
    ws_.drop(client);
}
void Server::flushPending() {
  for (uint8_t seat = 0; seat < kPlayers; ++seat) {
    if (!pending_[seat] || socket_[seat] == kNoConnection) continue;
    if (ws_.writable(static_cast<uint8_t>(socket_[seat]))) sendState(seat, pending_[seat] == 2);
  }
}
void Server::heartbeat(uint32_t now) {
  lastPing_ = now;
  for (uint8_t client = 0; client < kTrackedSlots; ++client) {
    if (!liveSocket_[client]) continue;
    if (!ws_.writable(client)) {
      if (!stalledSince_[client]) stalledSince_[client] = now ? now : 1;  // 0 = not stalled
      if (static_cast<uint32_t>(now - stalledSince_[client]) < kStallDropMs) continue;
    } else {
      stalledSince_[client] = 0;
      if (unanswered_[client] < kMissedPings) {
        ++unanswered_[client];
        ws_.sendPing(client);  // the browser's pong resets unanswered_
        continue;
      }
    }
    ++dropped_;
    ws_.drop(client);  // DISCONNECTED clears liveSocket_ and the seat
    liveSocket_[client] = false;
  }
}
void Server::closeIdleSockets(uint32_t now) {
  // A socket that never joined or resumed (join screen left open, rejected
  // ninth player, stale tab) must not hold one of the bounded server slots.
  // The browser reconnects on demand when its user presses JOIN.
  for (uint8_t client = 0; client < kTrackedSlots; ++client) {
    if (!liveSocket_[client] || sessions_.seatOf(client) != kNone) continue;
    if (static_cast<uint32_t>(now - connectedAt_[client]) < kAuthGraceMs) continue;
    ++idleClosed_;
    closeSocket(client);  // DISCONNECTED clears liveSocket_
    liveSocket_[client] = false;
  }
}
// Clocks, auto mode and the wait for a missing player.
void Server::tickTable() {
  if (table_.tick(millis())) publishAll();
}
Server::Diagnostics Server::diagnostics() const {
  Diagnostics d{};
  if (!running_) return d;
  d.valid = true;
  d.freeHeap = ESP.getFreeHeap();
  d.minFreeHeap = ESP.getMinFreeHeap();
  d.maxAllocHeap = ESP.getMaxAllocHeap();
  d.freePsram = ESP.getFreePsram();
  uint8_t sockets = 0;
  for (uint8_t i = 0; i < kTrackedSlots; ++i)
    if (liveSocket_[i]) ++sockets;
  d.sockets = sockets;
  d.stations = mode_ == Mode::Hotspot ? static_cast<uint8_t>(WiFi.softAPgetStationNum()) : 0;
  const PublicView v = game_.publicView();
  for (const auto& p : v.players) {
    if (p.seated) ++d.seated;
    if (p.seated && p.connected) ++d.connected;
  }
  d.resetReason = static_cast<uint8_t>(esp_reset_reason());
  d.rejected = rejected_;
  d.takeovers = takeovers_;
  d.idleClosed = idleClosed_;
  d.sendFailures = sendFailures_;
  d.deferred = deferred_;
  d.dropped = dropped_;
  return d;
}
void Server::logDiagnostics(uint32_t now) {
  lastDiagLog_ = now;
  const Diagnostics d = diagnostics();
  (void)d;  // LOG_INF compiles away at LOG_LEVEL 0
  LOG_INF("CARDPARTY",
          "diag heap=%u min=%u maxblk=%u psram=%u ws=%u ap=%u seats=%u/%u "
          "rej=%u takeover=%u idle=%u sendfail=%u defer=%u drop=%u reset=%u",
          static_cast<unsigned>(d.freeHeap), static_cast<unsigned>(d.minFreeHeap),
          static_cast<unsigned>(d.maxAllocHeap), static_cast<unsigned>(d.freePsram), d.sockets, d.stations, d.connected,
          d.seated, d.rejected, d.takeovers, d.idleClosed, d.sendFailures, d.deferred, d.dropped, d.resetReason);
}
void Server::sendGamePage() {
  http_.sendHeader("Content-Encoding", "gzip");
  http_.sendHeader("Cache-Control", "no-store");
  http_.sendHeader("Cross-Origin-Resource-Policy", "same-origin");
  http_.sendHeader("X-Frame-Options", "DENY");
  http_.send_P(200, "text/html", CardPartyPageHtml, CardPartyPageHtmlCompressedSize);
}
void Server::configureRoutes() {
  if (routesConfigured_) return;
  http_.on(kPath, HTTP_GET, [this] { sendGamePage(); });
  http_.on("/", HTTP_GET, [this] { sendGamePage(); });
  http_.onNotFound([this] {
    // Avoid silently serving the game at arbitrary paths on existing Wi-Fi.
    http_.send(404, "text/plain", "Not Found");
  });
  routesConfigured_ = true;
}
void Server::sendError(uint8_t client, const char* code) {
  char response[96] = {};
  JsonWriter w(response, sizeof(response));
  w.raw("{\"type\":\"error\",\"code\":");
  w.quoted(code);
  w.raw("}");
  if (w.ok() && ws_.writable(client)) ws_.sendTXT(client, response, w.size());
}

const char* Server::seatLabel(uint8_t seat, char* target, size_t capacity) const {
  if (!target || !capacity) return "";
  const PublicView v = game_.publicView();
  if (seat >= kPlayers || !v.players[seat].seated) {
    target[0] = 0;
    return target;
  }
  if (playerIds_[seat])
    std::snprintf(target,capacity,"%s·%s",v.players[seat].name,word(playerWords_[seat]));
  else
    std::snprintf(target,capacity,"%s",v.players[seat].name);
  return target;
}
void Server::assignId(uint8_t seat, uint64_t requested) {
  if (seat >= kPlayers) return;
  bool collision = false;
  for (uint8_t i = 0; i < kPlayers; ++i)
    if (i != seat && requested && playerIds_[i] == requested) collision = true;
  uint64_t id = requested;
  if (!id || collision) {
    // Public identity, never an authorization secret.
    do {
      id = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
      collision = !id;
      for (uint8_t i = 0; i < kPlayers; ++i)
        if (i != seat && playerIds_[i] == id) collision = true;
    } while (collision);
  }
  playerIds_[seat] = id;
  uint8_t wordId = history::Book::wordFor(id);
  bool inUse;
  do {
    inUse = false;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (i != seat && playerIds_[i] && playerWords_[i] == wordId) inUse = true;
    if (inUse) wordId = static_cast<uint8_t>((wordId + 1) & 63u);
  } while (inUse);
  playerWords_[seat] = wordId;
}

void Server::sendState(uint8_t seat, bool welcome) {
  if (!running_ || seat >= kPlayers || socket_[seat] == kNoConnection) return;
  if (pending_[seat] == 2) welcome = true;  // a held-back ticket is never replaced by plain state
  if (!ws_.writable(static_cast<uint8_t>(socket_[seat]))) {
    if (pending_[seat] < (welcome ? 2 : 1)) pending_[seat] = welcome ? 2 : 1;
    ++deferred_;
    return;
  }
  pending_[seat] = 0;
  HandView privateView{};
  uint32_t revision = 0;
  if (!sessions_.privateHand(socket_[seat], tickets_[seat].secret, privateView, revision)) return;
  PublicView v = game_.publicView();
  JsonWriter w(message_, sizeof(message_));
  char token[33] = {};
  if (welcome) {
    encodeSecret(tickets_[seat].secret, token);
    w.raw("{\"type\":\"welcome\",\"token\":");
    w.quoted(token);
    w.raw(",\"revision\":");
    w.num(revision);
    char pid[17] = {};
    encodePlayerId(playerIds_[seat], pid);
    w.raw(",\"pid\":");
    w.quoted(pid);
    w.raw(",\"word\":");
    w.quoted(word(playerWords_[seat]));
  } else {
    w.raw("{\"type\":\"state\",\"revision\":");
    w.num(revision);
  }
  w.raw(",\"game\":{\"phase\":");
  w.quoted(phaseName(v.phase));
  w.raw(",\"lang\":");
  w.quoted(language_);
  w.raw(",\"round\":");
  w.num(v.round);
  w.raw(",\"rounds\":");
  w.num(game_.rules().rounds);
  w.raw(",\"pick\":");
  w.num(v.pick);
  w.raw(",\"draw\":");
  w.num(v.draw);
  w.raw(",\"czar\":");
  w.num(v.czar);
  w.raw(",\"czarName\":");
  if (v.czar < kPlayers)
    { char label[64] = {}; w.quoted(seatLabel(v.czar,label,sizeof(label))); }
  else
    w.quoted("");
  w.raw(",\"promptText\":");
  if (v.prompt < deck_.promptsCount)
    w.quoted(deck_.prompts[v.prompt].text);
  else
    w.quoted("");
  w.raw(",\"submitted\":");
  w.num(v.submitted);
  w.raw(",\"expected\":");
  w.num(v.expected);
  w.raw(",\"players\":[");
  for (uint8_t i = 0; i < kPlayers; ++i) {
    if (i) w.raw(",");
    const PublicPlayer& p = v.players[i];
    w.raw("{\"seated\":");
    w.boolean(p.seated);
    w.raw(",\"connected\":");
    w.boolean(p.connected);
    w.raw(",\"name\":");
    { char label[64] = {}; w.quoted(seatLabel(i,label,sizeof(label))); }
    w.raw(",\"points\":");
    w.num(p.points);
    w.raw("}");
  }
  w.raw("],\"submissions\":[");
  for (uint8_t i = 0; i < v.plays; ++i) {
    if (i) w.raw(",");
    w.raw("{\"cards\":[");
    for (uint8_t j = 0; j < v.pick; ++j) {
      if (j) w.raw(",");
      const uint16_t id = v.pile[i].cards[j];
      w.quoted(id < deck_.answersCount ? deck_.answers[id].text : "");
    }
    w.raw("]}");
  }
  w.raw("],\"winnerName\":");
  if (v.winner == kRando)
    w.quoted("RANDO CARDRISSIAN");
  else if (v.winner < kPlayers)
    { char label[64] = {}; w.quoted(seatLabel(v.winner,label,sizeof(label))); }
  else
    w.quoted("");
  w.raw(",\"winningText\":");
  if (v.selected < v.plays) {
    // No author identities or submitted cards leak before the reveal:
    // the public view deliberately zeroes the pile in other phases.
    w.raw("\"");
    for (uint8_t i = 0; i < v.pick; ++i) {
      const uint16_t id = v.pile[v.selected].cards[i];
      const char* line = id < deck_.answersCount ? deck_.answers[id].text : "";
      if (i) w.raw(" / ");
      w.escaped(line);
    }
    w.raw("\"");
  } else
    w.quoted("");
  // Public only after the reveal: an ordered, bounded array lets clients
  // render the complete winning sentence with highlighted answer spans.
  w.raw(",\"winningCards\":[");
  if (v.selected < v.plays) {
    for (uint8_t i = 0; i < v.pick; ++i) {
      if (i) w.raw(",");
      const uint16_t id = v.pile[v.selected].cards[i];
      w.quoted(id < deck_.answersCount ? deck_.answers[id].text : "");
    }
  }
  w.raw("]");
  const uint32_t now = millis();
  const char* clock = table_.clock() == Table::Clock::Answers ? "answers"
                      : table_.clock() == Table::Clock::Juror ? "juror"
                      : table_.clock() == Table::Clock::Next  ? "next"
                                                              : "";
  w.raw(",\"clock\":");
  w.quoted(clock);
  w.raw(",\"left\":");
  w.num(table_.left(now));
  w.raw(",\"extended\":");
  w.boolean(table_.extended());
  const uint8_t pausedBy = table_.pausedBy();
  w.raw(",\"pausedByHost\":");
  w.boolean(pausedBy == kHost);
  w.raw(",\"pausedBy\":");
  { char label[64] = {}; w.quoted(pausedBy < kPlayers ? seatLabel(pausedBy,label,sizeof(label)) : ""); }
  w.raw(",\"blockedOn\":");
  { char label[64] = {}; w.quoted(table_.blockedOn() < kPlayers ? seatLabel(table_.blockedOn(),label,sizeof(label)) : ""); }
  w.raw(",\"votes\":");
  w.num(table_.votes());
  w.raw(",\"endedByHost\":");
  w.boolean(table_.ended());
  w.raw("},\"private\":{\"isCzar\":");
  w.boolean(privateView.judge);
  w.raw(",\"pausedByMe\":");
  w.boolean(pausedBy == seat);
  w.raw(",\"voted\":");
  w.boolean(table_.voted(seat));
  w.raw(",\"submitted\":");
  w.boolean(privateView.submitted);
  w.raw(",\"hand\":[");
  for (uint8_t i = 0; i < privateView.size; ++i) {
    if (i) w.raw(",");
    const uint16_t id = privateView.cards[i];
    w.raw("{\"text\":");
    w.quoted(id < deck_.answersCount ? deck_.answers[id].text : "");
    w.raw("}");
  }
  w.raw("]}}");
  if (!w.ok()) {
    ++sendFailures_;
    LOG_ERR("CARDPARTY", "Outgoing view exceeded bounded buffer");
  } else if (!ws_.sendTXT(static_cast<uint8_t>(socket_[seat]), message_, w.size())) {
    ++sendFailures_;
  }
}
void Server::publishAll() {
  for (uint8_t i = 0; i < kPlayers; ++i)
    if (socket_[i] != kNoConnection) sendState(i, false);
}
void Server::onDisconnect(uint8_t client) {
  sessions_.disconnect(client);
  for (uint8_t i = 0; i < kPlayers; ++i)
    if (socket_[i] == client) {
      socket_[i] = kNoConnection;
      pending_[i] = 0;
    }
  publishAll();
}
void Server::onMessage(uint8_t client, const uint8_t* bytes, size_t size) {
  if (!running_) return;
  Command command{};
  if (!parse(bytes, size, command)) {
    sendError(client);
    return;
  }
  bool accepted = false;
  if (command.kind == Kind::Join) {
    uint8_t secret[kTicketBytes] = {};
    fillSecret(secret);
    SessionTicket ticket{};
    accepted = sessions_.join(client, command.name, secret, ticket);
    if (accepted) {
      tickets_[ticket.seat] = ticket;
      socket_[ticket.seat] = client;
      assignId(ticket.seat, command.playerId);
      if (history_) history_->upsert(playerIds_[ticket.seat], command.name, ++identitySequence_);
      sendState(ticket.seat, true);
    } else {
      ++rejected_;
      // Not secret: tells the player why, without revealing any seat data.
      sendError(client, sessions_.seatOf(client) != kNone ? "already_joined"
                        : game_.phase() != Phase::Lobby   ? "game_running"
                        : game_.lobbyFull()               ? "session_full"
                                                          : "rejected");
      return;
    }
  } else if (command.kind == Kind::Resume) {
    uint8_t fresh[kTicketBytes] = {};
    fillSecret(fresh);
    SessionTicket ticket{};
    accepted = sessions_.resume(client, command.secret, fresh, ticket);
    if (accepted) {
      const uint32_t previousSocket = socket_[ticket.seat];
      tickets_[ticket.seat] = ticket;
      socket_[ticket.seat] = client;
      sendState(ticket.seat, true);
      // Revoke AND release the stale connection. Authentication alone
      // invalidates its ticket, but a still-live socket otherwise consumes
      // one of the bounded X4 WebSocket slots indefinitely.
      // Ownership has already moved, so a late DISCONNECTED event from the
      // prior socket cannot disconnect the replacement seat.
      if (previousSocket != kNoConnection && previousSocket != client) {
        ++takeovers_;
        closeSocket(static_cast<uint8_t>(previousSocket));
      }
    }
  } else if (command.kind == Kind::Submit) {
    // Paused or waiting for a missing player: nobody moves.
    accepted =
        !table_.frozen() && sessions_.submit(client, command.secret, command.revision, command.slots, command.count);
  } else if (command.kind == Kind::Judge) {
    accepted = !table_.frozen() && sessions_.judge(client, command.secret, command.revision, command.selected);
  } else {
    const uint8_t seat = sessions_.authenticatedSeat(client, command.secret);
    const uint32_t now = millis();
    if (seat != kNone && command.kind == Kind::Pause)
      accepted = table_.pause(seat, now);
    else if (seat != kNone && command.kind == Kind::Unpause)
      accepted = table_.unpause(seat, now);
    else if (seat != kNone && command.kind == Kind::Again)
      accepted = table_.vote(seat);
  }
  if (!accepted) {
    ++rejected_;
    if (command.kind == Kind::Resume) {
      // The browser must discard stale sessionStorage tickets when the X4
      // starts a fresh game. Otherwise it keeps retrying a dead session.
      char expired[] = "{\"type\":\"expired\"}";
      if (ws_.writable(client)) ws_.sendTXT(client, expired, sizeof(expired) - 1);
    } else {
      sendError(client);
      // Duplicate or stale moves: resync the authenticated seat so its
      // browser re-enables controls with the current revision and phase.
      const uint8_t seat = sessions_.authenticatedSeat(client, command.secret);
      if (seat != kNone && socket_[seat] == client) sendState(seat, false);
    }
    return;
  }
  table_.tick(millis());
  publishAll();
}
// Host actions: the X4 is king, they work paused or not. The table tick arms
// the clock of a new phase; publishAll() then sends one frame per seat.
bool Server::hostStart() {
  if (!sessions_.hostStart()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostNext() {
  if (!sessions_.hostNext()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostClose() {
  if (!sessions_.hostClose()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostAbort() {
  if (!sessions_.hostAbort()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostRematch() {
  if (!sessions_.hostRematch()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostPause() {
  if (!table_.pause(kHost, millis())) return false;
  publishAll();
  return true;
}
bool Server::hostUnpause() {
  if (!table_.unpause(kHost, millis())) return false;
  publishAll();
  return true;
}
bool Server::hostAddTime() {
  if (!table_.addTime(millis())) return false;
  publishAll();
  return true;
}
bool Server::hostContinue() {
  if (!table_.continueWithout(millis())) return false;
  publishAll();
  return true;
}
bool Server::hostKick(uint8_t seat) {
  if (seat >= kPlayers) return false;
  const uint32_t client = socket_[seat];
  if (!sessions_.hostKick(seat)) return false;
  table_.seatGone(seat);
  tickets_[seat] = SessionTicket{};
  socket_[seat] = kNoConnection;
  playerIds_[seat] = 0;
  playerWords_[seat] = 0;
  pending_[seat] = 0;
  if (client != kNoConnection) {
    // Tell the phone why before letting the socket go.
    char kicked[] = "{\"type\":\"kicked\"}";
    if (ws_.writable(static_cast<uint8_t>(client)))
      ws_.sendTXT(static_cast<uint8_t>(client), kicked, sizeof(kicked) - 1);
    closeSocket(static_cast<uint8_t>(client));
  }
  table_.tick(millis());
  publishAll();
  return true;
}
bool Server::hostEnd() {
  if (!game_.endGame()) return false;
  table_.endedByHost();
  table_.tick(millis());
  publishAll();
  return true;
}
void Server::hostSettings(const Settings& settings) {
  table_.apply(settings, millis());
  if (running_) publishAll();
}
bool Server::hostDeckChanged() {
  if (!game_.reloadDeck()) return false;
  table_.tick(millis());
  publishAll();
  return true;
}
void Server::hostRefresh() {
  if (running_) publishAll();
}
}  // namespace web
}  // namespace cardparty
#else
#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace cardparty {
namespace web {
namespace {
// SDL scenario tests use the real game rules but entirely simulated seats:
// no TCP listener, no Wi-Fi, no credentials or real browser sessions.
bool submitSimulatedHands(Game& game) {
  const auto v = game.publicView();
  if (v.phase != Phase::Playing) return false;
  const uint8_t slots[kPickMax] = {0, 1, 2};
  for (uint8_t i = 0; i < kPlayers; ++i) {
    if (!v.players[i].seated || i == v.czar) continue;
    if (!game.submit(i, slots, v.pick)) return false;
  }
  return game.phase() == Phase::Judging;
}
bool judgeSimulatedRound(Game& game) {
  if (!submitSimulatedHands(game)) return false;
  const auto v = game.publicView();
  return game.judge(v.czar, 0) && game.phase() == Phase::Reveal;
}
}  // namespace
Server::~Server() = default;
bool Server::begin(Mode m) {
  mode_ = m;
  const char* scenario = std::getenv("CROSSPLAY_SIM_CARDPARTY_STATE");
  // A normal simulator launch remains unchanged: CardParty HTTP/WebSocket
  // cannot run through the hardware WiFi classes, by design.
  if (!scenario || !*scenario || std::strcmp(scenario, "setup") == 0) {
    running_ = false;
    return false;
  }
  if (game_.phase() != Phase::Lobby) return false;
  sessions_.reset();
  ssid_ = "SIMULATED-CAW";
  ip_ = "192.0.2.1";  // RFC5737 documentation IP (not routable)
  url_ = "http://192.0.2.1/cardparty";
  std::memcpy(password_, "SIMULATOR-ONLY", 15);
  password_[15] = 0;
  for (const char* name : {"ADA", "BERT", "CYRA"}) {
    if (game_.join(name) == kNone) return false;
  }
  if (std::strcmp(scenario, "lobby") == 0) {
    running_ = true;
  } else if (std::strcmp(scenario, "collect") == 0) {
    running_ = game_.start();
  } else if (std::strcmp(scenario, "judge") == 0) {
    running_ = game_.start() && submitSimulatedHands(game_);
  } else if (std::strcmp(scenario, "reveal") == 0) {
    running_ = game_.start() && judgeSimulatedRound(game_);
  } else if (std::strcmp(scenario, "finished") == 0) {
    running_ = game_.start();
    // Same ten-round game state transitions as the real production engine.
    for (int round = 0; running_ && round < 10; ++round) {
      running_ = judgeSimulatedRound(game_) && game_.nextRound();
    }
    running_ = running_ && game_.phase() == Phase::Finished;
  } else if (std::strcmp(scenario, "blocked") == 0) {
    // A needed answerer offline for 15 s.
    running_ = game_.start();
    const uint8_t away = static_cast<uint8_t>((game_.publicView().czar + 1) % 3);
    running_ = running_ && game_.connected(away, false);
    const uint32_t now = millis();  // the loop keeps ticking on this clock
    table_.tick(now - kBlockAfterMs);
    table_.tick(now);
  } else {
    running_ = game_.start();  // host screens over a running round
  }
  if (running_) {
    std::printf("CARDPARTY_SIM_STATE=%s PHASE=%u ROUND=%u\n", scenario, static_cast<unsigned>(game_.phase()),
                game_.publicView().round);
  } else {
    std::printf("CARDPARTY_SIM_FIXTURE_FAILED=%s\n", scenario);
  }
  return running_;
}
void Server::loop() {
  if (running_) table_.tick(millis());  // clocks run in the SDL build too
}
void Server::stop() {
  running_ = false;
  password_[0] = 0;
  ssid_.clear();
  ip_.clear();
  url_.clear();
}
bool Server::hostStart() { return sessions_.hostStart(); }
bool Server::hostNext() { return sessions_.hostNext(); }
bool Server::hostClose() { return sessions_.hostClose(); }
bool Server::hostAbort() { return sessions_.hostAbort(); }
bool Server::hostRematch() { return sessions_.hostRematch(); }
bool Server::hostPause() { return table_.pause(kHost, millis()); }
bool Server::hostUnpause() { return table_.unpause(kHost, millis()); }
bool Server::hostAddTime() { return table_.addTime(millis()); }
bool Server::hostContinue() { return table_.continueWithout(millis()); }
bool Server::hostKick(uint8_t seat) {
  if (!sessions_.hostKick(seat)) return false;
  table_.seatGone(seat);
  return true;
}
bool Server::hostEnd() {
  if (!game_.endGame()) return false;
  table_.endedByHost();
  return true;
}
void Server::hostSettings(const Settings& settings) { table_.apply(settings, millis()); }
bool Server::hostDeckChanged() { return game_.reloadDeck(); }
void Server::hostRefresh() {}
Server::Diagnostics Server::diagnostics() const { return Diagnostics{}; }
}  // namespace web
}  // namespace cardparty
#endif
