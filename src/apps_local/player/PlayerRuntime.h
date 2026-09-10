#pragma once

#include <cstddef>
#include <cstdint>

#include "GuestSession.h"
#include "PlayerService.h"
#include "PlayerStore.h"

namespace player {

enum class RuntimeStatus : uint8_t {
  NotStarted = 0,
  Ready,
  StorageUnavailable,
  VfsError,
  DatabaseError,
  GuestError,
};

// Device-wide player state. One store/service pair owns persistence and one
// local identity is active at a time. The guest is fixed-size RAM state; no
// map or heap-backed player cache is needed.
class PlayerRuntime {
 public:
  static constexpr size_t kPlayerListCapacity = 8;

  static PlayerRuntime& instance();

  bool begin();
  bool ready() const { return status_ == RuntimeStatus::Ready; }
  RuntimeStatus status() const { return status_; }

  PlayerStore& store() { return store_; }
  PlayerService& service() { return service_; }

  GuestSession& guest() { return guest_; }
  const GuestSession& guest() const { return guest_; }

  const Player* activePlayer() const { return hasActivePlayer_ ? &activePlayer_ : nullptr; }
  bool hasActivePlayer() const { return hasActivePlayer_; }

  // Authenticate and make a persisted profile the device-wide local player.
  AuthResult login(const PlayerId& id, const char* pin);

  // Convert the current guest into a persisted profile and select it.
  PlayerServiceResult registerGuest(const char* name, const char* pin, uint64_t createdAt);

  // Drop the selected registered profile and continue with a guest identity.
  PlayerServiceResult useGuest();

  StoreResult listPlayers(Player* out, size_t capacity, size_t& count) const;

  PlayerId currentPlayerId() const;

 private:
  PlayerRuntime();
  PlayerServiceResult ensureGuest();

  PlayerStore store_{};
  PlayerService service_;
  GuestSession guest_{};
  Player activePlayer_{};
  bool hasActivePlayer_ = false;
  RuntimeStatus status_ = RuntimeStatus::NotStarted;
};

inline PlayerRuntime& runtime() { return PlayerRuntime::instance(); }

}  // namespace player
