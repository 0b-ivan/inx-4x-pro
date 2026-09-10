#include "PlayerService.h"

#include <climits>

#include "PlayerName.h"

namespace player {
namespace {

struct XpPolicy {
  uint16_t win;
  uint16_t loss;
  uint16_t draw;
};

constexpr XpPolicy kXpPolicies[] = {
    {0, 0, 0},    // Unknown
    {50, 15, 25}, // Battleship
    {70, 20, 35}, // Chess
    {45, 15, 25}, // Checkers
    {35, 10, 18}, // Connect Four
    {40, 15, 20}, // Yahtzee
    {35, 12, 18}, // Knucklebones
    {50, 15, 25}, // Jaipur
    {45, 15, 22}, // Sea Salt
    {45, 15, 22}, // Toy Battle
};

constexpr size_t kXpPolicyCount = sizeof(kXpPolicies) / sizeof(kXpPolicies[0]);

bool validGame(const GameId game) {
  const size_t value = static_cast<size_t>(game);
  return value > 0 && value < kXpPolicyCount;
}

bool validResult(const MatchResult result) {
  return result == MatchResult::Player1Win || result == MatchResult::Player2Win || result == MatchResult::Draw;
}

uint16_t saturatingAdd16(const uint16_t value, const uint16_t add) {
  const uint32_t sum = static_cast<uint32_t>(value) + add;
  return sum > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(sum);
}

uint32_t saturatingAdd32(const uint32_t value, const uint32_t add) {
  if (UINT32_MAX - value < add) return UINT32_MAX;
  return value + add;
}

uint32_t seedFor(const PlayerId& id) {
  uint32_t hash = 2166136261U;
  for (const uint8_t byte : id.bytes) {
    hash ^= byte;
    hash *= 16777619U;
  }
  return hash;
}

MatchOutcome outcomeFor(const MatchResult result, const bool player1) {
  if (result == MatchResult::Draw) return MatchOutcome::Draw;
  const bool won = (player1 && result == MatchResult::Player1Win) ||
                   (!player1 && result == MatchResult::Player2Win);
  return won ? MatchOutcome::Win : MatchOutcome::Loss;
}

void applyOutcome(GameStats& stats, const MatchOutcome outcome, const uint16_t xp) {
  stats.xp = saturatingAdd32(stats.xp, xp);
  switch (outcome) {
    case MatchOutcome::Win:
      stats.wins = saturatingAdd32(stats.wins, 1U);
      stats.currentStreak = saturatingAdd32(stats.currentStreak, 1U);
      if (stats.currentStreak > stats.bestStreak) stats.bestStreak = stats.currentStreak;
      break;
    case MatchOutcome::Loss:
      stats.losses = saturatingAdd32(stats.losses, 1U);
      stats.currentStreak = 0;
      break;
    case MatchOutcome::Draw:
      stats.draws = saturatingAdd32(stats.draws, 1U);
      stats.currentStreak = 0;
      break;
  }
}

void applyOutcome(GuestGameStats& stats, const MatchOutcome outcome, const uint16_t xp) {
  stats.xp = saturatingAdd32(stats.xp, xp);
  switch (outcome) {
    case MatchOutcome::Win:
      stats.wins = saturatingAdd16(stats.wins, 1U);
      stats.currentStreak = saturatingAdd16(stats.currentStreak, 1U);
      if (stats.currentStreak > stats.bestStreak) stats.bestStreak = stats.currentStreak;
      break;
    case MatchOutcome::Loss:
      stats.losses = saturatingAdd16(stats.losses, 1U);
      stats.currentStreak = 0;
      break;
    case MatchOutcome::Draw:
      stats.draws = saturatingAdd16(stats.draws, 1U);
      stats.currentStreak = 0;
      break;
  }
}

GuestSession* findGuest(GuestSession* guests, const size_t guestCount, const PlayerId& id) {
  if (guests == nullptr) return nullptr;
  for (size_t i = 0; i < guestCount; ++i) {
    if (guests[i].active() && guests[i].id == id) return &guests[i];
  }
  return nullptr;
}

PlayerServiceResult loadPersistentStats(PlayerStore& store, const PlayerId& id, const GameId game,
                                        GameStats& out) {
  const StoreResult statsResult = store.getGameStats(id, game, out);
  if (statsResult == StoreResult::Ok) return PlayerServiceResult::Ok;
  if (statsResult != StoreResult::NotFound) return PlayerServiceResult::StorageError;

  Player player{};
  const StoreResult playerResult = store.getPlayer(id, player);
  if (playerResult == StoreResult::NotFound) return PlayerServiceResult::PlayerNotFound;
  if (playerResult != StoreResult::Ok) return PlayerServiceResult::StorageError;

  out = GameStats{};
  out.playerId = id;
  out.game = game;
  return PlayerServiceResult::Ok;
}

}  // namespace

PlayerServiceResult PlayerService::createGuest(GuestSession& out) {
  if (randomFill_ == nullptr) return PlayerServiceResult::RandomUnavailable;

  for (int attempt = 0; attempt < 4; ++attempt) {
    GuestSession candidate{};
    if (!randomFill_(randomContext_, candidate.id.bytes.data(), candidate.id.bytes.size())) {
      return PlayerServiceResult::RandomUnavailable;
    }
    if (candidate.id.empty()) continue;

    if (store_.isOpen()) {
      Player existing{};
      const StoreResult found = store_.getPlayer(candidate.id, existing);
      if (found == StoreResult::Ok) continue;
      if (found != StoreResult::NotFound) return PlayerServiceResult::StorageError;
    }

    candidate.callsign = roll(seedFor(candidate.id));
    if (!candidate.callsign.known()) continue;
    out = candidate;
    return PlayerServiceResult::Ok;
  }

  return PlayerServiceResult::RandomUnavailable;
}

uint16_t PlayerService::xpForMatch(const GameId game, const MatchOutcome outcome) {
  if (!validGame(game)) return 0;
  const XpPolicy& policy = kXpPolicies[static_cast<size_t>(game)];
  switch (outcome) {
    case MatchOutcome::Win:
      return policy.win;
    case MatchOutcome::Loss:
      return policy.loss;
    case MatchOutcome::Draw:
      return policy.draw;
  }
  return 0;
}

PlayerServiceResult PlayerService::matchFinished(const MatchFinishedEvent& event, GuestSession* guests,
                                                 const size_t guestCount) {
  if (!validGame(event.game) || !validResult(event.result) || event.player1.empty() || event.player2.empty() ||
      event.player1 == event.player2 || (guestCount > 0 && guests == nullptr)) {
    return PlayerServiceResult::InvalidArgument;
  }

  for (size_t i = 0; i < guestCount; ++i) {
    if (!guests[i].active()) continue;
    for (size_t j = i + 1; j < guestCount; ++j) {
      if (guests[j].active() && guests[i].id == guests[j].id) return PlayerServiceResult::InvalidArgument;
    }
  }

  GuestSession* guest1 = findGuest(guests, guestCount, event.player1);
  GuestSession* guest2 = findGuest(guests, guestCount, event.player2);

  GameStats persistent[2]{};
  size_t persistentCount = 0;
  GameStats* persistent1 = nullptr;
  GameStats* persistent2 = nullptr;

  if (guest1 == nullptr) {
    persistent1 = &persistent[persistentCount++];
    const PlayerServiceResult loaded = loadPersistentStats(store_, event.player1, event.game, *persistent1);
    if (loaded != PlayerServiceResult::Ok) return loaded;
  }
  if (guest2 == nullptr) {
    persistent2 = &persistent[persistentCount++];
    const PlayerServiceResult loaded = loadPersistentStats(store_, event.player2, event.game, *persistent2);
    if (loaded != PlayerServiceResult::Ok) return loaded;
  }

  const MatchOutcome outcome1 = outcomeFor(event.result, true);
  const MatchOutcome outcome2 = outcomeFor(event.result, false);

  GuestGameStats nextGuest1{};
  GuestGameStats nextGuest2{};
  if (guest1 != nullptr) {
    const GuestGameStats* current = guest1->statsFor(event.game);
    if (current == nullptr) return PlayerServiceResult::InvalidArgument;
    nextGuest1 = *current;
    applyOutcome(nextGuest1, outcome1, xpForMatch(event.game, outcome1));
  } else {
    applyOutcome(*persistent1, outcome1, xpForMatch(event.game, outcome1));
  }

  if (guest2 != nullptr) {
    const GuestGameStats* current = guest2->statsFor(event.game);
    if (current == nullptr) return PlayerServiceResult::InvalidArgument;
    nextGuest2 = *current;
    applyOutcome(nextGuest2, outcome2, xpForMatch(event.game, outcome2));
  } else {
    applyOutcome(*persistent2, outcome2, xpForMatch(event.game, outcome2));
  }

  if (persistentCount > 0) {
    const StoreResult saved = store_.saveGameStatsBatch(persistent, persistentCount);
    if (saved == StoreResult::NotFound) return PlayerServiceResult::PlayerNotFound;
    if (saved != StoreResult::Ok) return PlayerServiceResult::StorageError;
  }

  if (guest1 != nullptr) {
    *guest1->statsFor(event.game) = nextGuest1;
    guest1->completedMatches = saturatingAdd16(guest1->completedMatches, 1U);
  }
  if (guest2 != nullptr) {
    *guest2->statsFor(event.game) = nextGuest2;
    guest2->completedMatches = saturatingAdd16(guest2->completedMatches, 1U);
  }

  return PlayerServiceResult::Ok;
}

}  // namespace player
