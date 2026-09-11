#include "PlayerRuntime.h"

#include "PlayerSqliteVfs.h"

#if defined(SIMULATOR)
#include <random>
#else
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>
#endif

namespace player {
namespace {

// Keep the player database in a normal, explicit SD-card directory. The first
// implementation used a hidden /.crosspoint path and relied on a mkdir call at
// runtime. A dedicated /crossplay directory is easier to inspect on the card
// and avoids treating a failed hidden-directory setup as normal guest mode.
constexpr char kDatabasePath[] = "/crossplay/player.db";
constexpr char kPlayerDirectory[] = "/crossplay";

bool runtimeRandomFill(void*, uint8_t* output, const size_t size) {
  if (output == nullptr) return false;
#if defined(SIMULATOR)
  static std::random_device source;
  for (size_t i = 0; i < size; ++i) output[i] = static_cast<uint8_t>(source());
#else
  esp_fill_random(output, size);
#endif
  return true;
}

}  // namespace

PlayerRuntime::PlayerRuntime() : service_(store_, runtimeRandomFill, nullptr) {}

PlayerRuntime& PlayerRuntime::instance() {
  static PlayerRuntime value;
  return value;
}

bool PlayerRuntime::begin() {
  if (ready()) return true;

#if defined(SIMULATOR)
  const StoreResult openResult = store_.open(":memory:");
#else
  if (!Storage.ready()) {
    status_ = RuntimeStatus::StorageUnavailable;
    LOG_ERR("PLAYER_DB", "SD storage is not ready");
    return false;
  }

  // Create the directory through HalStorage's normal SD-card helper before
  // SQLite starts opening the main DB, journals or temporary files.
  if (!Storage.exists(kPlayerDirectory) && !Storage.ensureDirectoryExists(kPlayerDirectory)) {
    status_ = RuntimeStatus::StorageUnavailable;
    LOG_ERR("PLAYER_DB", "could not create player directory path=%s", kPlayerDirectory);
    return false;
  }

  if (!registerPlayerSqliteVfs()) {
    status_ = RuntimeStatus::VfsError;
    LOG_ERR("PLAYER_DB", "could not register SD-backed SQLite VFS");
    return false;
  }

  const StoreResult openResult = store_.open(kDatabasePath);
#endif

  if (openResult != StoreResult::Ok) {
    store_.close();
#if !defined(SIMULATOR)
    LOG_ERR("PLAYER_DB", "PlayerStore open/schema failed result=%u path=%s",
            static_cast<unsigned int>(openResult), kDatabasePath);
#endif
    // Games remain playable as a RAM guest if the database itself is corrupt or
    // unavailable, but callers can distinguish that state through
    // persistenceReady(). Login/registration never pretend persistence works.
    if (ensureGuest() == PlayerServiceResult::Ok) {
      status_ = RuntimeStatus::GuestOnly;
      return true;
    }
    status_ = RuntimeStatus::DatabaseError;
    return false;
  }

  if (ensureGuest() != PlayerServiceResult::Ok) {
    store_.close();
    status_ = RuntimeStatus::GuestError;
    return false;
  }

  status_ = RuntimeStatus::Ready;
#if !defined(SIMULATOR)
  LOG_INF("PLAYER_DB", "player database ready path=%s", kDatabasePath);
#endif
  return true;
}

PlayerServiceResult PlayerRuntime::ensureGuest() {
  if (guest_.active()) return PlayerServiceResult::Ok;
  return service_.createGuest(guest_);
}

AuthResult PlayerRuntime::login(const PlayerId& id, const char* pin) {
  if (!ready()) return AuthResult::StorageError;
  const AuthResult authResult = service_.authenticate(id, pin);
  if (authResult != AuthResult::Success) return authResult;

  Player player{};
  if (store_.getPlayer(id, player) != StoreResult::Ok) return AuthResult::StorageError;
  activePlayer_ = player;
  hasActivePlayer_ = true;
  return AuthResult::Success;
}

PlayerServiceResult PlayerRuntime::registerGuest(const char* name, const char* pin, const uint64_t createdAt) {
  if (!persistenceReady()) return PlayerServiceResult::StorageError;
  if (ensureGuest() != PlayerServiceResult::Ok) return PlayerServiceResult::RandomUnavailable;

  Player player{};
  const PlayerServiceResult result = service_.registerGuest(guest_, name, pin, createdAt, player);
  if (result != PlayerServiceResult::Ok) return result;

  activePlayer_ = player;
  hasActivePlayer_ = true;
  return PlayerServiceResult::Ok;
}

PlayerServiceResult PlayerRuntime::useGuest() {
  if (!ready()) return PlayerServiceResult::StorageError;
  hasActivePlayer_ = false;
  activePlayer_ = Player{};
  return ensureGuest();
}

PlayerServiceResult PlayerRuntime::recordCurrentMatch(const GameId game, const MatchOutcome outcome) {
  if (!ready() && !begin()) return PlayerServiceResult::StorageError;

  const PlayerId playerId = currentPlayerId();
  if (playerId.empty()) return PlayerServiceResult::PlayerNotFound;
  if (hasActivePlayer_) return service_.recordOutcome(playerId, game, outcome, nullptr);
  return service_.recordOutcome(playerId, game, outcome, &guest_);
}

StoreResult PlayerRuntime::listPlayers(Player* out, const size_t capacity, size_t& count) const {
  if (!ready()) {
    count = 0;
    return StoreResult::NotOpen;
  }
  if (!persistenceReady()) {
    count = 0;
    return StoreResult::Ok;
  }
  return store_.listPlayers(out, capacity, count);
}

StoreResult PlayerRuntime::currentStats(GameStats* out, const size_t capacity, size_t& count) const {
  count = 0;
  if (!ready()) return StoreResult::NotOpen;
  if (out == nullptr || capacity < kGameCount) return StoreResult::InvalidArgument;

  const PlayerId playerId = currentPlayerId();
  if (playerId.empty()) return StoreResult::NotFound;

  for (size_t index = 0; index < kGameCount; ++index) {
    const GameId game = static_cast<GameId>(index + 1U);
    GameStats value{};
    value.playerId = playerId;
    value.game = game;

    if (hasActivePlayer_) {
      const StoreResult result = store_.getGameStats(playerId, game, value);
      if (result != StoreResult::Ok && result != StoreResult::NotFound) return result;
      if (result == StoreResult::NotFound) {
        value = GameStats{};
        value.playerId = playerId;
        value.game = game;
      }
    } else {
      const GuestGameStats* guestStats = guest_.statsFor(game);
      if (guestStats == nullptr) return StoreResult::InvalidArgument;
      value.wins = guestStats->wins;
      value.losses = guestStats->losses;
      value.draws = guestStats->draws;
      value.currentStreak = guestStats->currentStreak;
      value.bestStreak = guestStats->bestStreak;
      value.xp = guestStats->xp;
    }

    out[count++] = value;
  }

  return StoreResult::Ok;
}

PlayerId PlayerRuntime::currentPlayerId() const {
  if (hasActivePlayer_) return activePlayer_.id;
  if (guest_.active()) return guest_.id;
  return PlayerId{};
}

}  // namespace player
