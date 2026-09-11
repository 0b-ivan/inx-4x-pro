#include "PlayerWebApi.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../leaderboard/RankSystem.h"
#include "PlayerName.h"
#include "PlayerProgression.h"
#include "PlayerRuntime.h"

namespace playerweb {
namespace {

const char* className(const player::PlayerClass value) {
  switch (value) {
    case player::PlayerClass::Commander:
      return "COMMANDER";
    case player::PlayerClass::Strategist:
      return "STRATEGIST";
    case player::PlayerClass::Tactician:
      return "TACTICIAN";
    case player::PlayerClass::FortuneSeeker:
      return "FORTUNE SEEKER";
    case player::PlayerClass::AllRounder:
      return "ALLROUNDER";
  }
  return "ALLROUNDER";
}

const char* runtimeMode(const player::PlayerRuntime& runtime) {
  if (runtime.persistenceReady()) return "persistent";
  if (runtime.ready()) return "guest-only";
  return "offline";
}

void appendEscaped(std::string& out, const char* value) {
  out.push_back('"');
  if (value != nullptr) {
    for (const unsigned char ch : std::string(value)) {
      switch (ch) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (ch < 0x20) {
            char escaped[7]{};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(ch));
            out += escaped;
          } else {
            out.push_back(static_cast<char>(ch));
          }
          break;
      }
    }
  }
  out.push_back('"');
}

std::string idHex(const player::PlayerId& id) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(player::PlayerId::kSize * 2U);
  for (size_t i = 0; i < player::PlayerId::kSize; ++i) {
    out[i * 2U] = kHex[(id.bytes[i] >> 4U) & 0x0FU];
    out[i * 2U + 1U] = kHex[id.bytes[i] & 0x0FU];
  }
  return out;
}

int nibble(const char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return 10 + value - 'a';
  if (value >= 'A' && value <= 'F') return 10 + value - 'A';
  return -1;
}

bool parseId(const char* text, player::PlayerId& out) {
  if (text == nullptr || std::strlen(text) != player::PlayerId::kSize * 2U) return false;
  for (size_t i = 0; i < player::PlayerId::kSize; ++i) {
    const int high = nibble(text[i * 2U]);
    const int low = nibble(text[i * 2U + 1U]);
    if (high < 0 || low < 0) return false;
    out.bytes[i] = static_cast<uint8_t>((high << 4U) | low);
  }
  return text[player::PlayerId::kSize * 2U] == '\0' && !out.empty();
}

const char* authMessage(const player::AuthResult result) {
  switch (result) {
    case player::AuthResult::Success:
      return "PLAYER SELECTED";
    case player::AuthResult::WrongPin:
      return "WRONG PIN";
    case player::AuthResult::Locked:
      return "PIN LOCKED UNTIL RESTART";
    case player::AuthResult::InvalidArgument:
      return "PIN MUST BE 4 DIGITS";
    case player::AuthResult::PlayerNotFound:
      return "PLAYER NOT FOUND";
    case player::AuthResult::CredentialMissing:
      return "PLAYER HAS NO PIN";
    case player::AuthResult::StorageError:
      return "PLAYER STORAGE OFFLINE";
    case player::AuthResult::CryptoError:
      return "PIN CHECK FAILED";
  }
  return "LOGIN FAILED";
}

const char* registrationMessage(const player::PlayerServiceResult result) {
  switch (result) {
    case player::PlayerServiceResult::Ok:
      return "PROFILE SAVED";
    case player::PlayerServiceResult::GuestNotEligible:
      return "PLAY ONE MATCH FIRST";
    case player::PlayerServiceResult::NameTaken:
      return "NAME ALREADY USED";
    case player::PlayerServiceResult::InvalidPin:
      return "PIN MUST BE 4 DIGITS";
    case player::PlayerServiceResult::InvalidArgument:
      return "INVALID PLAYER NAME";
    case player::PlayerServiceResult::RandomUnavailable:
      return "RANDOM SOURCE FAILED";
    case player::PlayerServiceResult::PlayerNotFound:
      return "PLAYER NOT FOUND";
    case player::PlayerServiceResult::CryptoError:
      return "PIN SETUP FAILED";
    case player::PlayerServiceResult::StorageError:
      return "PLAYER STORAGE OFFLINE";
  }
  return "REGISTRATION FAILED";
}

Reply snapshot(const bool ok, const char* message, const int status) {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready()) runtime.begin();

  Reply reply;
  reply.status = status;
  std::string& out = reply.body;
  out.reserve(1800);
  out += "{\"ok\":";
  out += ok ? "true" : "false";
  out += ",\"message\":";
  appendEscaped(out, message == nullptr ? "" : message);
  out += ",\"mode\":";
  appendEscaped(out, runtimeMode(runtime));
  out += ",\"persistence\":";
  out += runtime.persistenceReady() ? "true" : "false";

  if (!runtime.ready()) {
    out += ",\"current\":null,\"players\":[],\"profile\":null}";
    return reply;
  }

  const player::Player* active = runtime.activePlayer();
  const bool guest = active == nullptr;
  char callsign[player::kMaxNameLength + 1]{};
  const char* visibleName = "GUEST";
  player::PlayerId currentId{};
  if (active != nullptr) {
    visibleName = active->name;
    player::compose(callsign, sizeof(callsign), active->callsign);
    currentId = active->id;
  } else if (runtime.guest().active()) {
    player::compose(callsign, sizeof(callsign), runtime.guest().callsign);
    currentId = runtime.guest().id;
  }

  out += ",\"current\":{\"id\":";
  appendEscaped(out, idHex(currentId).c_str());
  out += ",\"name\":";
  appendEscaped(out, visibleName);
  out += ",\"callsign\":";
  appendEscaped(out, callsign);
  out += ",\"registered\":";
  out += guest ? "false" : "true";
  out += ",\"completedMatches\":";
  out += std::to_string(guest ? runtime.guest().completedMatches : 0U);
  out += ",\"canRegister\":";
  out += (runtime.persistenceReady() && guest && runtime.guest().completedMatches > 0) ? "true" : "false";
  out += "}";

  std::array<player::Player, player::PlayerRuntime::kPlayerListCapacity> players{};
  size_t playerCount = 0;
  runtime.listPlayers(players.data(), players.size(), playerCount);
  out += ",\"players\":[";
  for (size_t i = 0; i < playerCount; ++i) {
    if (i != 0) out.push_back(',');
    out += "{\"id\":";
    appendEscaped(out, idHex(players[i].id).c_str());
    out += ",\"name\":";
    appendEscaped(out, players[i].name);
    out += "}";
  }
  out += "]";

  std::array<player::GameStats, player::PlayerRuntime::kGameCount> stats{};
  size_t statsCount = 0;
  if (runtime.currentStats(stats.data(), stats.size(), statsCount) != player::StoreResult::Ok) {
    out += ",\"profile\":null}";
    return reply;
  }

  const player::ProgressionSnapshot progression = player::ProgressionSystem::summarize(stats.data(), statsCount);
  player::GameStats battleship{};
  battleship.game = player::GameId::Battleship;
  for (size_t i = 0; i < statsCount; ++i) {
    if (stats[i].game == player::GameId::Battleship) {
      battleship = stats[i];
      break;
    }
  }
  const leaderboard::Rank rank = leaderboard::RankSystem::forWins(battleship.wins);

  out += ",\"profile\":{\"xp\":" + std::to_string(progression.xp);
  out += ",\"level\":" + std::to_string(progression.level);
  out += ",\"class\":";
  appendEscaped(out, className(progression.playerClass));
  out += ",\"radar\":[";
  out += std::to_string(progression.style.strategy) + ",";
  out += std::to_string(progression.style.tactics) + ",";
  out += std::to_string(progression.style.precision) + ",";
  out += std::to_string(progression.style.risk) + ",";
  out += std::to_string(progression.style.endurance) + ",";
  out += std::to_string(progression.style.versatility) + "]";
  out += ",\"battleship\":{\"wins\":" + std::to_string(battleship.wins);
  out += ",\"losses\":" + std::to_string(battleship.losses);
  out += ",\"draws\":" + std::to_string(battleship.draws);
  out += ",\"streak\":" + std::to_string(battleship.currentStreak);
  out += ",\"bestStreak\":" + std::to_string(battleship.bestStreak);
  out += ",\"rank\":";
  appendEscaped(out, rank.ranked() ? rank.name : "NO RANK");
  out += ",\"nextRankWins\":" + std::to_string(leaderboard::RankSystem::nextThreshold(battleship.wins));
  out += "}}}";
  return reply;
}

}  // namespace

Reply state() { return snapshot(true, "", 200); }

Reply useGuest() {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready() && !runtime.begin()) return snapshot(false, "PLAYER RUNTIME OFFLINE", 503);
  const player::PlayerServiceResult result = runtime.useGuest();
  return snapshot(result == player::PlayerServiceResult::Ok,
                  result == player::PlayerServiceResult::Ok ? "GUEST SELECTED" : "GUEST UNAVAILABLE",
                  result == player::PlayerServiceResult::Ok ? 200 : 409);
}

Reply login(const char* playerIdHex, const char* pin) {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready() && !runtime.begin()) return snapshot(false, "PLAYER RUNTIME OFFLINE", 503);
  if (!runtime.persistenceReady()) return snapshot(false, "PLAYER STORAGE OFFLINE", 503);

  player::PlayerId id{};
  if (!parseId(playerIdHex, id)) return snapshot(false, "INVALID PLAYER ID", 400);
  const player::AuthResult result = runtime.login(id, pin);
  return snapshot(result == player::AuthResult::Success, authMessage(result),
                  result == player::AuthResult::Success ? 200 : 401);
}

Reply registerGuest(const char* name, const char* pin) {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready() && !runtime.begin()) return snapshot(false, "PLAYER RUNTIME OFFLINE", 503);
  if (!runtime.persistenceReady()) return snapshot(false, "PLAYER STORAGE OFFLINE", 503);
  if (runtime.hasActivePlayer()) return snapshot(false, "SELECT GUEST FIRST", 409);

  const std::time_t now = std::time(nullptr);
  const uint64_t createdAt = now > 0 ? static_cast<uint64_t>(now) : 0U;
  const player::PlayerServiceResult result = runtime.registerGuest(name, pin, createdAt);
  return snapshot(result == player::PlayerServiceResult::Ok, registrationMessage(result),
                  result == player::PlayerServiceResult::Ok ? 200 : 409);
}

Reply stepGuestCallsign(const int slot) {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready() && !runtime.begin()) return snapshot(false, "PLAYER RUNTIME OFFLINE", 503);
  if (runtime.hasActivePlayer()) return snapshot(false, "SELECT GUEST FIRST", 409);
  if (!runtime.guest().active() || slot < 0 || slot >= player::kSlotCount) return snapshot(false, "INVALID SLOT", 400);
  runtime.guest().callsign = player::nextWord(runtime.guest().callsign, slot);
  return snapshot(true, "CALLSIGN UPDATED", 200);
}

std::string displayName() {
  player::PlayerRuntime& runtime = player::runtime();
  if (!runtime.ready()) runtime.begin();
  if (const player::Player* active = runtime.activePlayer()) return active->name;
  if (runtime.ready() && runtime.guest().active()) {
    char callsign[player::kMaxNameLength + 1]{};
    player::compose(callsign, sizeof(callsign), runtime.guest().callsign);
    if (callsign[0] != '\0') return callsign;
  }
  return "X4 PRO";
}

}  // namespace playerweb
