#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

// Shared browser multiplayer seat/session protocol. No Arduino or game
// dependencies: Battleship is the first adapter, other browser games can
// reuse the same first-claim, authorization, reconnect and throttling rules.
namespace webplay {

class SeatSession final {
 public:
  using RandomWord = uint32_t (*)();

  void begin(RandomWord entropy) {
    owner_ = -1;
    commandSeen_ = false;
    lastCommandMs_ = 0;
    rotate(token_, entropy);
    rotate(resumeToken_, entropy);
  }

  void stop() {
    owner_ = -1;
    commandSeen_ = false;
    lastCommandMs_ = 0;
    token_[0] = '\0';
    resumeToken_[0] = '\0';
  }

  bool hasOwner() const { return owner_ >= 0; }
  uint8_t owner() const { return static_cast<uint8_t>(owner_); }
  const char* token() const { return token_; }
  const char* resumeToken() const { return resumeToken_; }

  // The ordinary bootstrap token is public by design and is insufficient
  // to read or mutate the device's local player account data.
  bool authorized(const char* bootstrap, const char* seatSecret) const {
    return hasOwner() && match(token_, bootstrap) && match(resumeToken_, seatSecret);
  }

  // Call before dispatching an application command. Games decide if the
  // command is legal; this only enforces the private seat and replay gates.
  bool admit(uint8_t client, const char* bootstrap, bool newProfile,
             bool resume, const char* seatSecret, uint32_t now) {
    if (!match(token_, bootstrap) ||
        (hasOwner() && owner_ != client) ||
        (!hasOwner() && !newProfile && !resume) ||
        (resume && !match(resumeToken_, seatSecret))) return false;
    // Wraparound-safe monotonic throttle; commands rejected by the game
    // still consume a slot to prevent rapid invalid move spam.
    if (commandSeen_ && static_cast<uint32_t>(now - lastCommandMs_) < 100) return false;
    commandSeen_ = true;
    lastCommandMs_ = now;
    return true;
  }

  // Commit a first claim only after the game adapter accepted the action.
  void claim(uint8_t client, bool newProfile, RandomWord entropy) {
    if (hasOwner()) return;
    owner_ = client;
    if (newProfile) rotate(resumeToken_, entropy);
  }

  // Revokes the active transport seat, preserving the private resume token
  // so the same browser can rejoin its confirmed game after a network drop.
  bool disconnect(uint8_t client, RandomWord entropy) {
    if (!hasOwner() || owner_ != client) return false;
    owner_ = -1;
    commandSeen_ = false;
    rotate(token_, entropy);
    return true;
  }

 private:
  static bool match(const char* expected, const char* candidate) {
    return expected[0] && candidate && std::strlen(candidate) == 32 &&
           std::strcmp(expected, candidate) == 0;
  }
  static void rotate(char (&out)[33], RandomWord entropy) {
    if (!entropy) { out[0] = '\0'; return; }
    for (int i = 0; i < 4; ++i)
      std::snprintf(out + i * 8, 9, "%08lx", static_cast<unsigned long>(entropy()));
  }

  char token_[33] = {};
  char resumeToken_[33] = {};
  int owner_ = -1;
  uint32_t lastCommandMs_ = 0;
  bool commandSeen_ = false;
};
}  // namespace webplay
