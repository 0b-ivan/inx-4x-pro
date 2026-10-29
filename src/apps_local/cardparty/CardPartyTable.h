#pragma once
// Host-side match control around Game: the answer clock with its one-time
// extension, the Juror clock, auto mode, pause, and the wait for a needed
// player who dropped. Time comes in as millis(); no clock, storage or
// network in here, so it runs unchanged in host tests.
#include <cstdint>

#include "CardPartyCore.h"

namespace cardparty {

constexpr uint8_t kHost = 253;  // pausedBy(): the X4 itself
// A needed player offline this long blocks the table until the host decides.
constexpr uint32_t kBlockAfterMs = 15000;
// +TIME when no automatic extension is set.
constexpr uint8_t kDefaultExtraSecs = 15;

struct Settings {
  uint8_t rounds = 10;
  uint8_t answerSecs = 0;      // 0: no answer clock
  uint8_t extensionSecs = 15;  // added once when the answer clock runs out early
  uint8_t jurorSecs = 0;       // 0: the Juror takes as long as needed
  uint8_t autoSecs = 0;        // 0: the host starts every round
  bool rando = true;
};

class Table final {
 public:
  enum class Clock : uint8_t { None, Answers, Juror, Next };
  explicit Table(Game& game) : game_(game) {}

  // New match: clocks, pause, block and votes cleared; settings kept.
  void reset() {
    clock_ = Clock::None;
    deadline_ = left_ = 0;
    extended_ = endedByHost_ = false;
    pausedBy_ = blockedOn_ = kNone;
    votes_ = 0;
    for (auto& since : offlineSince_) since = 0;
    lastPhase_ = game_.phase();
    lastRound_ = game_.publicView().round;
  }

  const Settings& settings() const { return settings_; }
  // The answer clock length applies from the next round, the rest now.
  // Rounds only count for the next match (Game::configure).
  void apply(const Settings& s, uint32_t now) {
    settings_ = s;
    game_.setRando(s.rando);
    const Phase phase = game_.phase();
    if (phase == Phase::Judging) setClock(s.jurorSecs ? Clock::Juror : Clock::None, s.jurorSecs, now);
    if (phase == Phase::Reveal) setClock(s.autoSecs ? Clock::Next : Clock::None, s.autoSecs, now);
  }

  // Call after every host or player action and from the loop. True when
  // anything the players see has changed.
  bool tick(uint32_t now) {
    bool changed = sync(now);
    const PublicView v = game_.publicView();
    for (uint8_t i = 0; i < kPlayers; ++i) {
      if (!v.players[i].seated || v.players[i].connected)
        offlineSince_[i] = 0;
      else if (!offlineSince_[i])
        offlineSince_[i] = now ? now : 1;  // 0 means online
    }
    uint8_t blocked = kNone;
    for (uint8_t i = 0; i < kPlayers && blocked == kNone; ++i)
      if (game_.needed(i) && offlineSince_[i] && now - offlineSince_[i] >= kBlockAfterMs) blocked = i;
    if (blocked != blockedOn_) {
      const bool was = frozen();
      blockedOn_ = blocked;
      refreeze(was, now);
      changed = true;
    }
    if (!frozen() && clock_ != Clock::None && static_cast<int32_t>(now - deadline_) >= 0) {
      expire(now);
      sync(now);
      changed = true;
    }
    return changed;
  }

  bool pause(uint8_t by, uint32_t now) {
    const Phase phase = game_.phase();
    if (pausedBy_ != kNone || phase == Phase::Lobby || phase == Phase::Finished) return false;
    if (by != kHost && (by >= kPlayers || !game_.publicView().players[by].seated)) return false;
    const bool was = frozen();
    pausedBy_ = by;
    refreeze(was, now);
    return true;
  }
  // Only whoever paused, or the host.
  bool unpause(uint8_t by, uint32_t now) {
    if (pausedBy_ == kNone || (by != kHost && by != pausedBy_)) return false;
    const bool was = frozen();
    pausedBy_ = kNone;
    refreeze(was, now);
    return true;
  }
  // Host +TIME on a running answer or Juror clock.
  bool addTime(uint32_t now) {
    if (clock_ != Clock::Answers && clock_ != Clock::Juror) return false;
    const uint32_t extra = (settings_.extensionSecs ? settings_.extensionSecs : kDefaultExtraSecs) * 1000u;
    if (frozen())
      left_ += extra;
    else if (static_cast<int32_t>(now - deadline_) >= 0)
      deadline_ = now + extra;
    else
      deadline_ += extra;
    return true;
  }
  // Host answer to a block: the round goes on without the missing player.
  // A missing Juror ends the round without points.
  bool continueWithout(uint32_t now) {
    const uint8_t seat = blockedOn_;
    if (seat == kNone) return false;
    if (game_.phase() == Phase::Judging)
      game_.abortRound();
    else
      game_.releaseTurn(seat);
    tick(now);
    return true;
  }
  // "Again!" from a phone after the match. The host decides.
  bool vote(uint8_t seat) {
    if (game_.phase() != Phase::Finished || seat >= kPlayers || !game_.publicView().players[seat].seated) return false;
    votes_ |= static_cast<uint8_t>(1u << seat);
    return true;
  }
  void endedByHost() { endedByHost_ = true; }
  // A removed seat takes its vote and its offline record with it.
  void seatGone(uint8_t seat) {
    if (seat >= kPlayers) return;
    votes_ &= static_cast<uint8_t>(~(1u << seat));
    offlineSince_[seat] = 0;
  }

  Clock clock() const { return clock_; }
  // Milliseconds left on the running clock, frozen while paused or blocked.
  uint32_t left(uint32_t now) const {
    if (clock_ == Clock::None) return 0;
    if (frozen()) return left_;
    return static_cast<int32_t>(deadline_ - now) > 0 ? deadline_ - now : 0;
  }
  bool extended() const { return extended_; }
  uint8_t pausedBy() const { return pausedBy_; }
  uint8_t blockedOn() const { return blockedOn_; }
  bool frozen() const { return pausedBy_ != kNone || blockedOn_ != kNone; }
  bool ended() const { return endedByHost_; }
  uint8_t votes() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (votes_ & (1u << i)) ++n;
    return n;
  }
  bool voted(uint8_t seat) const { return seat < kPlayers && (votes_ & (1u << seat)); }

 private:
  // Arms the clock of a phase the game just entered.
  bool sync(uint32_t now) {
    const PublicView v = game_.publicView();
    if (v.phase == lastPhase_ && v.round == lastRound_) return false;
    if (lastPhase_ == Phase::Finished && v.phase != Phase::Finished) {
      votes_ = 0;
      endedByHost_ = false;
    }
    lastPhase_ = v.phase;
    lastRound_ = v.round;
    extended_ = false;
    if (v.phase == Phase::Playing)
      setClock(settings_.answerSecs ? Clock::Answers : Clock::None, settings_.answerSecs, now);
    else if (v.phase == Phase::Judging)
      setClock(settings_.jurorSecs ? Clock::Juror : Clock::None, settings_.jurorSecs, now);
    else if (v.phase == Phase::Reveal)
      setClock(settings_.autoSecs ? Clock::Next : Clock::None, settings_.autoSecs, now);
    else
      setClock(Clock::None, 0, now);
    return true;
  }
  void setClock(Clock clock, uint8_t secs, uint32_t now) {
    clock_ = clock;
    left_ = secs * 1000u;
    deadline_ = now + left_;
  }
  void expire(uint32_t now) {
    const Clock clock = clock_;
    clock_ = Clock::None;
    if (clock == Clock::Answers) {
      const PublicView v = game_.publicView();
      if (!extended_ && settings_.extensionSecs && v.submitted < v.expected) {
        extended_ = true;
        clock_ = Clock::Answers;
        deadline_ = now + settings_.extensionSecs * 1000u;
        return;
      }
      if (!game_.closeSubmissions()) game_.abortRound();  // nobody answered
    } else if (clock == Clock::Juror) {
      game_.abortRound();  // no points
    } else if (clock == Clock::Next) {
      game_.nextRound();  // fewer than three online: waits for the host
    }
  }
  // The clock stands still while paused or blocked.
  void refreeze(bool was, uint32_t now) {
    const bool is = frozen();
    if (!was && is)
      left_ = static_cast<int32_t>(deadline_ - now) > 0 ? deadline_ - now : 0;
    else if (was && !is)
      deadline_ = now + left_;
  }

  Game& game_;
  Settings settings_{};
  Clock clock_ = Clock::None;
  uint32_t deadline_ = 0, left_ = 0;
  bool extended_ = false, endedByHost_ = false;
  uint8_t pausedBy_ = kNone, blockedOn_ = kNone;
  uint8_t votes_ = 0;
  uint32_t offlineSince_[kPlayers] = {};
  Phase lastPhase_ = Phase::Lobby;
  uint16_t lastRound_ = 0;
};

}  // namespace cardparty
