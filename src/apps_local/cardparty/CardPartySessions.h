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
  bool join(uint32_t connection, const char* name,
            const uint8_t freshSecret[kTicketBytes], SessionTicket& ticket) {
    if (connection == kNoConnection || !freshSecret || !validSecret(freshSecret) ||
        connectionInUse(connection) || secretInUse(freshSecret)) return false;
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

  // No loss of cards or points. An interrupted client must prove its ticket
  // before reclaiming the same seat with a fresh connection and new secret.
  bool disconnect(uint32_t connection) {
    for (uint8_t i = 0; i < kPlayers; ++i) {
      Seat& state = seats_[i];
      if (!state.registered || !state.connected || state.connection != connection)
        continue;
      state.connected = false;
      state.connection = kNoConnection;
      game_.connected(i, false);
      return true;
    }
    return false;
  }

  bool resume(uint32_t connection, const uint8_t oldSecret[kTicketBytes],
              const uint8_t freshSecret[kTicketBytes], SessionTicket& ticket) {
    if (connection == kNoConnection || !oldSecret || !freshSecret ||
        !validSecret(oldSecret) || !validSecret(freshSecret) ||
        connectionInUse(connection) || secretInUse(freshSecret)) return false;

    for (uint8_t i = 0; i < kPlayers; ++i) {
      Seat& state = seats_[i];
      if (!state.registered || state.connected ||
          !sameSecret(state.secret, oldSecret)) continue;
      if (!game_.connected(i, true)) return false;
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
  bool submit(uint32_t connection, const uint8_t secret[kTicketBytes],
              uint32_t expectedRevision, const uint8_t* slots, uint8_t size) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision ||
        seats_[seat].revision == UINT32_MAX) return false;
    if (!game_.submit(seat, slots, size)) return false;
    ++seats_[seat].revision;
    return true;
  }

  bool judge(uint32_t connection, const uint8_t secret[kTicketBytes],
             uint32_t expectedRevision, uint8_t chosen) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision ||
        seats_[seat].revision == UINT32_MAX) return false;
    if (!game_.judge(seat, chosen)) return false;
    ++seats_[seat].revision;
    return true;
  }

  bool leaveLobby(uint32_t connection, const uint8_t secret[kTicketBytes],
                  uint32_t expectedRevision) {
    const uint8_t seat = authenticate(connection, secret);
    if (seat == kNone || seats_[seat].revision != expectedRevision) return false;
    if (!game_.leaveLobby(seat)) return false;
    seats_[seat] = Seat{};
    return true;
  }

  bool privateHand(uint32_t connection, const uint8_t secret[kTicketBytes],
                   HandView& view, uint32_t& revision) const {
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

  bool connectionInUse(uint32_t connection) const {
    for (const auto& s : seats_)
      if (s.registered && s.connected && s.connection == connection) return true;
    return false;
  }

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
      if (s.registered && s.connected && s.connection == connection &&
          sameSecret(s.secret, secret)) return i;
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
