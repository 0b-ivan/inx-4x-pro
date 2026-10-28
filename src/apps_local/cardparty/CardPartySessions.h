#pragma once
// Cards Against Whatever: small, allocation-free authenticated seat gateway.
//
// The X4 transport must pass authenticated socket identifiers and 128-bit
// secrets sourced from ESP hardware entropy (esp_fill_random). This module
// does NOT generate secrets or expose them in public game snapshots.
// This is not encryption: clients on an unprotected WLAN can still observe
// plain HTTP/WebSocket traffic. Prefer a protected hotspot when practical.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CardPartyCore.h"

namespace cardparty {

constexpr size_t kTicketBytes = 16;
constexpr uint32_t kNoConnection = 0xffffffffu;

struct SessionTicket {
  uint8_t seat = kNone;
  uint32_t revision = 0;
  uint8_t secret[kTicketBytes] = {};
};

class Sessions final {
 public:
  explicit Sessions(Game& game) : game_(game) {}

  // Call after Game::configure on a new match. The host transport must also
  // disconnect all existing WebSockets and invalidate any cached room codes.
  void reset() {
    for (auto& s : seats_) s = Seat{};
  }

  // The transport MUST draw 16 cryptographically random bytes, never seed from
  // Game's predictable card RNG. No secret gets returned to other clients.
  bool join(uint32_t connection, const char* name, const uint8_t freshSecret[kTicketBytes], SessionTicket& ticket) {
    if (connection == kNoConnection || !freshSecret || !validSecret(freshSecret) || connectionInUse(connection) ||
        secretInUse(freshSecret) || !Game::validName(name))
      return false;
    // A full lobby may reclaim a seat whose browser is gone (tab switched,
    // QR opened in a second browser). Live seats are never displaced.
    if (game_.lobbyFull()) {
      const uint8_t ghost = game_.disconnectedLobbySeat();
      if (ghost == kNone || !game_.leaveLobby(ghost)) return false;
      seats_[ghost] = Seat{};
    }
    const uint8_t seat = game_.join(name);
    if (seat == kNone) return false;
    Seat& state = seats_[seat];
    state = Seat{};
    state.registered = true;
    state.connected = true;
    state.connection = connection;
    std::memcpy(state.secret, freshSecret, kTicketBytes);
    publishTicket(seat, ticket);
    return true;
  }

  // An interrupted or refreshed client must prove its existing ticket
  // before reclaiming the same seat with a new connection and rotated secret.
  // A browser reload can open the new socket before the old socket has closed.
  // Authorized takeover invalidates the old connection immediately.
  bool disconnect(uint32_t connection) {
    for (uint8_t i = 0; i < kPlayers; ++i) {
      Seat& state = seats_[i];
      if (!state.registered || !state.connected || state.connection != connection) continue;
      state.connected = false;
      state.connection = kNoConnection;
      game_.connected(i, false);
      return true;
    }
    return false;
  }

  bool resume(uint32_t connection, const uint8_t oldSecret[kTicketBytes], const uint8_t freshSecret[kTicketBytes],
              SessionTicket& ticket) {
    if (connection == kNoConnection || !oldSecret || !freshSecret || !validSecret(oldSecret) ||
        !validSecret(freshSecret) || secretInUse(freshSecret))
      return false;
    const uint8_t bound = seatOf(connection);

    for (uint8_t i = 0; i < kPlayers; ++i) {
      Seat& state = seats_[i];
      if (!state.registered || !sameSecret(state.secret, oldSecret)) continue;
      // One connection never controls two seats; re-proving the own seat on
      // the same socket is harmless and simply rotates the ticket.
      if (bound != kNone && bound != i) return false;
      // The connection itself does not establish identity: possession of the
      // 128-bit ticket authorizes a handover, even while the previous socket
      // is still considered live. Subsequent frames from that old socket
      // fail both connection and token authentication.
      if (!state.connected && !game_.connected(i, true)) return false;
      state.connected = true;
      state.connection = connection;
      std::memcpy(state.secret, freshSecret, kTicketBytes);
      // An old token, including any stored in sessionStorage, is now revoked.
      publishTicket(i, ticket);
      return true;
    }
    return false;
  }

  // An exact expected revision makes duplicate WebSocket deliveries idempotent
  // by rejection. Only accepted moves advance the issuing seat's revision.
  bool submit(uint32_t connection, const uint8_t secret[kTicketBytes], uint32_t expectedRevision, const uint8_t* slots,
              uint8_t size) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision || seats_[seat].revision == UINT32_MAX) return false;
    if (!game_.submit(seat, slots, size)) return false;
    ++seats_[seat].revision;
    return true;
  }

  bool judge(uint32_t connection, const uint8_t secret[kTicketBytes], uint32_t expectedRevision, uint8_t chosen) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision || seats_[seat].revision == UINT32_MAX) return false;
    if (!game_.judge(seat, chosen)) return false;
    ++seats_[seat].revision;
    return true;
  }

  bool leaveLobby(uint32_t connection, const uint8_t secret[kTicketBytes], uint32_t expectedRevision) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision) return false;
    if (!game_.leaveLobby(seat)) return false;
    seats_[seat] = Seat{};
    return true;
  }

  bool privateHand(uint32_t connection, const uint8_t secret[kTicketBytes], HandView& view, uint32_t& revision) const {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || !game_.privateHand(seat, view)) return false;
    revision = seats_[seat].revision;
    return true;
  }

  // These are host-only methods. NEVER connect them directly to browser
  // commands; the X4 physical UI invokes them after a local decision.
  bool hostStart() { return game_.start(); }
  bool hostNext() { return game_.nextRound(); }
  bool hostClose() { return game_.closeSubmissions(); }
  bool hostAbort() { return game_.abortRound(); }
  // Seat bindings and tickets survive so every phone lands in the new lobby.
  bool hostRematch() { return game_.rematch(); }
  // Removes the player and revokes their ticket; they may join a later lobby.
  bool hostKick(uint8_t seat) {
    if (seat >= kPlayers || !game_.kick(seat)) return false;
    seats_[seat] = Seat{};
    return true;
  }

  // Seat proven by BOTH the live connection and its current ticket.
  uint8_t authenticatedSeat(uint32_t connection, const uint8_t secret[kTicketBytes]) const {
    return authenticate(connection, secret);
  }

  // Seat bound to a live connection, or kNone.
  uint8_t seatOf(uint32_t connection) const {
    if (connection == kNoConnection) return kNone;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (seats_[i].registered && seats_[i].connected && seats_[i].connection == connection) return i;
    return kNone;
  }

  PublicView publicView() const { return game_.publicView(); }

 private:
  struct Seat {
    bool registered = false;
    bool connected = false;
    uint32_t connection = kNoConnection;
    uint32_t revision = 0;
    uint8_t secret[kTicketBytes] = {};
  };

  static bool validSecret(const uint8_t* secret) {
    uint8_t seen = 0;
    for (size_t i = 0; i < kTicketBytes; ++i) seen |= secret[i];
    return seen != 0;
  }

  static bool sameSecret(const uint8_t* a, const uint8_t* b) {
    uint8_t diff = 0;
    for (size_t i = 0; i < kTicketBytes; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
  }

  bool connectionInUse(uint32_t connection) const { return seatOf(connection) != kNone; }

  bool secretInUse(const uint8_t* secret) const {
    for (const auto& s : seats_)
      if (s.registered && sameSecret(s.secret, secret)) return true;
    return false;
  }

  uint8_t authenticate(uint32_t connection, const uint8_t* secret) const {
    if (!secret || connection == kNoConnection) return kNone;
    // Do not trust seat IDs from WebSocket frames. Connection AND ticket must
    // match an already-bound seat; do not disclose seat on failed proof.
    for (uint8_t i = 0; i < kPlayers; ++i) {
      const Seat& s = seats_[i];
      if (s.registered && s.connected && s.connection == connection && sameSecret(s.secret, secret)) return i;
    }
    return kNone;
  }

  void publishTicket(uint8_t seat, SessionTicket& out) const {
    out = SessionTicket{};
    out.seat = seat;
    out.revision = seats_[seat].revision;
    std::memcpy(out.secret, seats_[seat].secret, kTicketBytes);
  }

  Game& game_;
  Seat seats_[kPlayers] = {};
};

}  // namespace cardparty
