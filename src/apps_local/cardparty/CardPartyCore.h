#pragma once
// Cards Against Whatever: deterministic, freestanding rules core.
//
// No storage, HTTP, authentication or UI dependencies. The transport must
// authenticate each seat and NEVER serialize internal Game state.
#include <cstdint>
#include <cstring>

namespace cardparty {
constexpr uint8_t kPlayers = 8, kHandMax = 12, kPickMax = 3, kPileMax = 8;
constexpr uint8_t kNone = 255, kRando = 254;
constexpr uint16_t kNoCard = 65535;
enum class Phase : uint8_t { Lobby, Playing, Judging, Reveal, Finished };
struct Prompt {
  const char* text;
  uint8_t pick, draw;
};
struct Answer {
  const char* text;
};
struct Deck {
  const Prompt* prompts = nullptr;
  uint16_t promptsCount = 0;
  const Answer* answers = nullptr;
  uint16_t answersCount = 0;
  // Optional category masks per card (SD packs). Null: every card plays.
  const uint16_t* promptMasks = nullptr;
  const uint16_t* answerMasks = nullptr;
};
struct Rules {
  uint8_t hand = 10, rounds = 10;
  bool rando = false;
};
struct PublicPlayer {
  char name[24] = {};
  bool seated = false, connected = false;
  uint16_t points = 0;
};
struct PublicPlay {
  uint16_t cards[kPickMax] = {kNoCard, kNoCard, kNoCard};
  uint8_t author = kNone;  // never populated before Reveal
};
struct PublicView {
  Phase phase = Phase::Lobby;
  uint8_t czar = kNone, pick = 0, draw = 0;
  uint16_t round = 0, prompt = kNoCard;
  uint8_t submitted = 0, expected = 0, plays = 0, selected = kNone, winner = kNone;
  bool aborted = false;
  PublicPlayer players[kPlayers] = {};
  PublicPlay pile[kPileMax] = {};
};
struct HandView {
  uint8_t size = 0;
  uint16_t cards[kHandMax] = {};
  bool submitted = false, judge = false;
};

class Game {
 public:
  using Blocker = bool (*)(void*, uint16_t, uint16_t);
  void setBlocker(Blocker callback, void* context) { blocker_ = callback; blockerContext_ = context; }
  static bool validDeck(const Deck& deck, const Rules& rules) {
    if (!deck.prompts || !deck.answers || !deck.promptsCount || !deck.answersCount || deck.promptsCount > 4096 ||
        deck.answersCount > 4096 || rules.hand < 3 || rules.hand > 10)
      return false;
    for (uint16_t i = 0; i < deck.promptsCount; ++i) {
      const Prompt& p = deck.prompts[i];
      if (!p.text || !p.text[0] || p.pick < 1 || p.pick > 3 || p.draw > 2 || rules.hand + p.draw > kHandMax ||
          p.pick > rules.hand + p.draw)
        return false;
    }
    for (uint16_t i = 0; i < deck.answersCount; ++i)
      if (!deck.answers[i].text || !deck.answers[i].text[0]) return false;
    return true;
  }
  bool configure(const Deck& deck, Rules rules, uint32_t seed) {
    if (!validDeck(deck, rules)) return false;
    deck_ = &deck;
    rules_ = rules;
    rng_ = seed ? seed : 0x9e3779b9u;
    phase_ = Phase::Lobby;
    count_ = 0;
    round_ = 0;
    prompt_ = kNoCard;
    czar_ = kNone;
    plays_ = 0;
    selected_ = kNone;
    winner_ = kNone;
    aborted_ = false;
    for (auto& player : players_) player = Player{};
    for (auto& play : pile_) play = Play{};
    clearDrawn();
    return true;
  }
  static bool validName(const char* name) {
    if (!name) return false;
    size_t n = 0;
    while (name[n] && n <= 23) ++n;
    return n > 0 && n <= 23;
  }
  bool lobbyFull() const { return phase_ == Phase::Lobby && count_ >= kPlayers; }
  uint8_t join(const char* name) {
    if (phase_ != Phase::Lobby || !deck_ || !validName(name)) return kNone;
    size_t n = 0;
    while (name[n] && n < 23) ++n;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (!players_[i].seated) {
        players_[i] = Player{};
        Player& p = players_[i];
        std::memcpy(p.name, name, n);
        p.name[n] = 0;
        p.seated = p.connected = true;
        ++count_;
        return i;
      }
    return kNone;
  }
  bool leaveLobby(uint8_t seat) {
    if (phase_ != Phase::Lobby || !valid(seat)) return false;
    players_[seat] = Player{};
    --count_;
    return true;
  }
  // A pending answerer who drops keeps their turn open ("held") until the
  // transport calls releaseTurn() after its grace window, or until the host
  // closes submissions. Phone browsers drop WebSockets on every screen lock
  // or app switch; the slowest player must not be skipped by a 2 s blip.
  bool connected(uint8_t seat, bool value) {
    if (!valid(seat)) return false;
    Player& p = players_[seat];
    p.connected = value;
    p.held = !value && phase_ == Phase::Playing && seat != czar_ && !p.submitted;
    if (phase_ == Phase::Playing) maybeJudge();
    return true;
  }
  bool held(uint8_t seat) const { return valid(seat) && players_[seat].held; }
  bool releaseTurn(uint8_t seat) {
    if (!valid(seat) || !players_[seat].held) return false;
    players_[seat].held = false;
    if (phase_ == Phase::Playing) maybeJudge();
    return true;
  }
  bool start() {
    if (phase_ != Phase::Lobby || active() < 3 || !promptsOn()) return false;
    if (answersOn() < answersNeeded(count_)) return false;
    for (auto& p : players_)
      if (p.seated)
        while (p.size < rules_.hand) {
          const uint16_t c = draw();
          if (c == kNoCard) return false;
          p.hand[p.size++] = c;
        }
    czar_ = next(kNone);
    round_ = 1;
    return beginRound();
  }
  bool submit(uint8_t seat, const uint8_t* slots, uint8_t n) {
    if (phase_ != Phase::Playing || !valid(seat) || !players_[seat].connected || seat == czar_ || !slots ||
        n != pick_ || players_[seat].submitted || plays_ >= kPileMax)
      return false;
    Player& p = players_[seat];
    for (uint8_t i = 0; i < n; ++i) {
      if (slots[i] >= p.size) return false;
      for (uint8_t j = 0; j < i; ++j)
        if (slots[j] == slots[i]) return false;
    }
    Play& play = pile_[plays_++];
    play = Play{};
    play.author = seat;
    p.submitted = true;
    for (uint8_t i = 0; i < n; ++i) {
      p.selected[i] = slots[i];
      play.cards[i] = p.hand[slots[i]];
    }
    maybeJudge();
    return true;
  }
  // Host-only action for timeout/disconnect. Never expose unguarded to clients.
  bool closeSubmissions() {
    if (phase_ != Phase::Playing || humanPlays() == 0) return false;
    shuffle();
    phase_ = Phase::Judging;
    return true;
  }
  bool judge(uint8_t seat, uint8_t selected) {
    if (phase_ != Phase::Judging || seat != czar_ || !valid(seat) || !players_[seat].connected || selected >= plays_)
      return false;
    selected_ = selected;
    winner_ = pile_[selected].author;
    if (winner_ < kPlayers) ++players_[winner_].points;
    phase_ = Phase::Reveal;
    return true;
  }
  bool abortRound() {
    if (phase_ != Phase::Playing && phase_ != Phase::Judging) return false;
    phase_ = Phase::Reveal;
    winner_ = kNone;
    selected_ = kNone;
    aborted_ = true;
    return true;
  }
  bool nextRound() {
    if (phase_ != Phase::Reveal) return false;
    if (rules_.rounds && round_ >= rules_.rounds) {
      phase_ = Phase::Finished;
      return true;
    }
    if (active() < 3) return false;
    for (auto& p : players_)
      if (p.seated) {
        uint8_t w = 0;
        for (uint8_t i = 0; i < p.size; ++i) {
          bool played = false;
          if (p.submitted)
            for (uint8_t j = 0; j < pick_; ++j)
              if (p.selected[j] == i) played = true;
          if (!played) p.hand[w++] = p.hand[i];
        }
        // Unplayed Draw-Two extras expire after the current round.
        if (w > rules_.hand) w = rules_.hand;
        p.size = w;
        p.submitted = false;
        for (auto& slot : p.selected) slot = kNone;
      }
    for (auto& p : players_)
      if (p.seated)
        while (p.size < rules_.hand) {
          const uint16_t c = draw();
          if (c == kNoCard) return false;
          p.hand[p.size++] = c;
        }
    czar_ = next(czar_);
    ++round_;
    return beginRound();
  }
  // Same seats and names, fresh match: hands, points and round state reset.
  // Keeps the network session alive so phones need not rejoin the hotspot.
  bool rematch() {
    if (phase_ != Phase::Finished || !deck_) return false;
    phase_ = Phase::Lobby;
    round_ = 0;
    prompt_ = kNoCard;
    czar_ = kNone;
    pick_ = draw_ = 0;
    plays_ = 0;
    selected_ = kNone;
    winner_ = kNone;
    aborted_ = false;
    for (auto& play : pile_) play = Play{};
    for (auto& p : players_) {
      if (!p.seated) continue;
      Player fresh{};
      fresh.seated = true;
      fresh.connected = p.connected;
      std::memcpy(fresh.name, p.name, sizeof(fresh.name));
      p = fresh;
    }
    return true;
  }
  // Category switches: a card plays only if none of its categories is off.
  // Dealt cards stay; the switch applies to the next draw and prompt.
  void setEnabled(uint16_t enabled) { enabled_ = enabled; }
  uint16_t enabled() const { return enabled_; }
  bool promptOn(uint16_t i) const {
    return deck_ && i < deck_->promptsCount && (!deck_->promptMasks || (deck_->promptMasks[i] & ~enabled_) == 0);
  }
  bool answerOn(uint16_t i) const {
    return deck_ && i < deck_->answersCount && (!deck_->answerMasks || (deck_->answerMasks[i] & ~enabled_) == 0);
  }
  uint16_t promptsOn() const {
    uint16_t n = 0;
    if (deck_)
      for (uint16_t i = 0; i < deck_->promptsCount; ++i)
        if (promptOn(i)) ++n;
    return n;
  }
  uint16_t answersOn() const {
    uint16_t n = 0;
    if (deck_)
      for (uint16_t i = 0; i < deck_->answersCount; ++i)
        if (answerOn(i)) ++n;
    return n;
  }
  // Distinct answers `seats` players need: their hands, the largest draw for
  // everyone but the Juror, and Rando's cards.
  uint16_t answersNeeded(uint8_t seats) const {
    uint8_t maxDraw = 0;
    if (deck_)
      for (uint16_t i = 0; i < deck_->promptsCount; ++i)
        if (promptOn(i) && deck_->prompts[i].draw > maxDraw) maxDraw = deck_->prompts[i].draw;
    return static_cast<uint16_t>(seats * rules_.hand + (seats ? seats - 1 : 0) * maxDraw + (rules_.rando ? 3 : 0));
  }
  // Rando joins or leaves from the next round on.
  void setRando(bool on) { rules_.rando = on; }
  // Match length; fixed once the match has started.
  bool setRounds(uint8_t rounds) {
    if (phase_ != Phase::Lobby) return false;
    rules_.rounds = rounds;
    return true;
  }
  const Rules& rules() const { return rules_; }
  uint8_t seatedCount() const { return count_; }
  // The game cannot move on without this player right now: the Juror while
  // judging, or an answerer whose turn is still open.
  bool needed(uint8_t seat) const {
    if (!valid(seat)) return false;
    const Player& p = players_[seat];
    if (phase_ == Phase::Judging) return seat == czar_;
    if (phase_ == Phase::Playing) return seat != czar_ && !p.submitted && (p.connected || p.held);
    return false;
  }
  // Host removes a player. A removed Juror ends the round without points;
  // an answer they already handed in leaves the pile while it is still
  // private, and plays on without an author once it is shown. Fewer than
  // three seats left ends the game.
  bool kick(uint8_t seat) {
    if (!valid(seat)) return false;
    if (phase_ == Phase::Lobby) return leaveLobby(seat);
    const bool juror = seat == czar_ && (phase_ == Phase::Playing || phase_ == Phase::Judging);
    if (phase_ == Phase::Playing) {
      uint8_t w = 0;
      for (uint8_t i = 0; i < plays_; ++i)
        if (pile_[i].author != seat) pile_[w++] = pile_[i];
      plays_ = w;
    } else {
      for (uint8_t i = 0; i < plays_; ++i)
        if (pile_[i].author == seat) pile_[i].author = kNone;
      if (winner_ == seat) winner_ = kNone;
    }
    players_[seat] = Player{};
    --count_;
    if (juror) {
      phase_ = Phase::Reveal;
      winner_ = kNone;
      selected_ = kNone;
      aborted_ = true;
    } else if (phase_ == Phase::Playing)
      maybeJudge();
    if (phase_ != Phase::Finished && count_ < 3) finish();
    return true;
  }
  // Host ends the match now; the standings so far are final.
  bool endGame() {
    if (phase_ == Phase::Lobby || phase_ == Phase::Finished) return false;
    finish();
    return true;
  }
  // The Deck behind deck_ now holds other cards (a pack switch). In the
  // lobby that is all; between rounds every hand is dealt again and the next
  // round starts. Points stay.
  bool reloadDeck() {
    if (!deck_ || !validDeck(*deck_, rules_)) return false;
    clearDrawn();
    if (phase_ == Phase::Lobby) return true;
    if (phase_ != Phase::Reveal) return false;
    plays_ = 0;
    selected_ = kNone;
    winner_ = kNone;
    aborted_ = false;
    prompt_ = kNoCard;
    for (auto& p : players_) {
      p.size = 0;
      p.submitted = false;
      p.held = false;
      for (auto& slot : p.selected) slot = kNone;
    }
    if (rules_.rounds && round_ >= rules_.rounds) {
      phase_ = Phase::Finished;
      return true;
    }
    for (auto& p : players_)
      if (p.seated)
        while (p.size < rules_.hand) {
          const uint16_t c = draw();
          if (c == kNoCard) return false;
          p.hand[p.size++] = c;
        }
    czar_ = next(czar_);
    ++round_;
    return beginRound();
  }
  // Lobby seat whose browser is gone; reclaimable when the lobby is full.
  uint8_t disconnectedLobbySeat() const {
    if (phase_ != Phase::Lobby) return kNone;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (players_[i].seated && !players_[i].connected) return i;
    return kNone;
  }
  PublicView publicView() const {
    PublicView v{};
    v.phase = phase_;
    v.czar = czar_;
    v.round = round_;
    v.prompt = prompt_;
    v.pick = pick_;
    v.draw = draw_;
    v.submitted = humanPlays();
    v.expected = expected();
    for (uint8_t i = 0; i < kPlayers; ++i) {
      const Player& p = players_[i];
      auto& q = v.players[i];
      q.seated = p.seated;
      q.connected = p.connected;
      q.points = p.points;
      std::memcpy(q.name, p.name, sizeof(q.name));
    }
    if (phase_ == Phase::Judging || phase_ == Phase::Reveal || phase_ == Phase::Finished) {
      v.plays = plays_;
      for (uint8_t i = 0; i < plays_; ++i) {
        for (uint8_t j = 0; j < pick_; ++j) v.pile[i].cards[j] = pile_[i].cards[j];
        // Never reveal author in judgment stage, including Rando identity.
        if (phase_ != Phase::Judging) v.pile[i].author = pile_[i].author;
      }
    }
    if (phase_ == Phase::Reveal || phase_ == Phase::Finished) {
      v.winner = winner_;
      v.selected = selected_;
      v.aborted = aborted_;
    }
    return v;
  }
  // Server must check the requesting client's authenticated seat first.
  bool privateHand(uint8_t seat, HandView& out) const {
    if (!valid(seat)) return false;
    out = HandView{};
    const Player& p = players_[seat];
    out.size = p.size;
    out.submitted = p.submitted;
    out.judge = (seat == czar_);
    for (uint8_t i = 0; i < p.size; ++i) out.cards[i] = p.hand[i];
    return true;
  }
  Phase phase() const { return phase_; }

 private:
  struct Player {
    bool seated = false, connected = false, submitted = false, held = false;
    char name[24] = {};
    uint16_t hand[kHandMax] = {};
    uint8_t size = 0;
    uint8_t selected[kPickMax] = {kNone, kNone, kNone};
    uint16_t points = 0;
  };
  struct Play {
    uint8_t author = kNone;
    uint16_t cards[kPickMax] = {kNoCard, kNoCard, kNoCard};
  };
  bool valid(uint8_t seat) const { return seat < kPlayers && players_[seat].seated; }
  uint8_t active() const {
    uint8_t n = 0;
    for (const auto& p : players_)
      if (p.seated && p.connected) ++n;
    return n;
  }
  uint8_t next(uint8_t prev) const {
    for (uint8_t d = 1; d <= kPlayers; ++d) {
      const uint8_t i = static_cast<uint8_t>((prev + d) % kPlayers);
      if (players_[i].seated && players_[i].connected) return i;
    }
    return kNone;
  }
  // Final standings. A round cut short shows nothing of its private pile.
  void finish() {
    if (phase_ == Phase::Playing || phase_ == Phase::Judging) {
      plays_ = 0;
      selected_ = kNone;
      winner_ = kNone;
    }
    phase_ = Phase::Finished;
  }
  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }
  // Shuffled draw pile without storing one: every playable answer not yet
  // drawn is equally likely. Drawn cards (in hands, played, discarded) stay
  // out until the pile is used up; then it is reshuffled from everything
  // not currently in a hand or Rando's play.
  static bool drawnBit(const uint32_t* set, uint16_t i) { return (set[i >> 5] >> (i & 31)) & 1u; }
  static void markDrawn(uint32_t* set, uint16_t i) { set[i >> 5] |= 1u << (i & 31); }
  void clearDrawn() {
    std::memset(drawnAnswers_, 0, sizeof(drawnAnswers_));
    std::memset(drawnPrompts_, 0, sizeof(drawnPrompts_));
  }
  bool answerFree(uint16_t c) const {
    return !drawnBit(drawnAnswers_, c) && answerOn(c) &&
           !(blocker_ && prompt_ != kNoCard && blocker_(blockerContext_, prompt_, c));
  }
  void reshuffleAnswers() {
    std::memset(drawnAnswers_, 0, sizeof(drawnAnswers_));
    for (const auto& p : players_)
      if (p.seated)
        for (uint8_t j = 0; j < p.size; ++j)
          if (p.hand[j] < deck_->answersCount) markDrawn(drawnAnswers_, p.hand[j]);
    for (uint8_t i = 0; i < plays_; ++i)
      if (pile_[i].author == kRando)
        for (uint8_t j = 0; j < kPickMax; ++j)
          if (pile_[i].cards[j] < deck_->answersCount) markDrawn(drawnAnswers_, pile_[i].cards[j]);
  }
  // Uniform pick among `free` cards: a few random probes (cheap while most
  // cards are free), else count and select the k-th one.
  template <class Free>
  uint16_t pickFree(uint16_t n, Free free) {
    for (uint8_t t = 0; t < 32; ++t) {
      const uint16_t c = static_cast<uint16_t>(random() % n);
      if (free(c)) return c;
    }
    uint16_t count = 0;
    for (uint16_t c = 0; c < n; ++c)
      if (free(c)) ++count;
    if (!count) return kNoCard;
    uint16_t k = static_cast<uint16_t>(random() % count);
    for (uint16_t c = 0; c < n; ++c)
      if (free(c) && k-- == 0) return c;
    return kNoCard;
  }
  uint16_t draw() {
    const uint16_t n = deck_->answersCount;
    auto free = [this](uint16_t c) { return answerFree(c); };
    uint16_t c = pickFree(n, free);
    if (c == kNoCard) {
      reshuffleAnswers();
      c = pickFree(n, free);
    }
    if (c != kNoCard) markDrawn(drawnAnswers_, c);
    return c;
  }
  uint8_t expected() const {
    // A disconnected player whose cards are already in the pile must
    // *still* count toward the round's expected answer total. Otherwise
    // dropping the Wi-Fi link after submitting could prematurely advance
    // to Judging while another connected player has not yet answered.
    // Players who left before submitting block only while their turn is held.
    uint8_t n = 0;
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (i != czar_ && players_[i].seated && (players_[i].connected || players_[i].submitted || players_[i].held)) ++n;
    return n;
  }
  uint8_t humanPlays() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < plays_; ++i)
      if (pile_[i].author != kRando) ++n;
    return n;
  }
  void shuffle() {
    for (uint8_t i = plays_; i > 1; --i) {
      const uint8_t j = static_cast<uint8_t>(random() % i);
      const Play x = pile_[i - 1];
      pile_[i - 1] = pile_[j];
      pile_[j] = x;
    }
  }
  void maybeJudge() {
    if (phase_ == Phase::Playing && humanPlays() > 0 && humanPlays() >= expected()) {
      shuffle();
      phase_ = Phase::Judging;
    }
  }
  bool beginRound() {
    phase_ = Phase::Playing;
    plays_ = 0;
    selected_ = kNone;
    winner_ = kNone;
    aborted_ = false;
    // No prompt comes twice until every playable prompt has been shown in
    // this session; then the pile starts over, never with the last prompt.
    const uint16_t count = deck_->promptsCount;
    auto fresh = [this](uint16_t c) { return promptOn(c) && !drawnBit(drawnPrompts_, c); };
    uint16_t chosen = pickFree(count, fresh);
    if (chosen == kNoCard) {
      std::memset(drawnPrompts_, 0, sizeof(drawnPrompts_));
      if (prompt_ < count && promptsOn() > 1) markDrawn(drawnPrompts_, prompt_);
      chosen = pickFree(count, fresh);
    }
    if (chosen == kNoCard) return false;
    markDrawn(drawnPrompts_, chosen);
    prompt_ = chosen;
    pick_ = deck_->prompts[prompt_].pick;
    draw_ = deck_->prompts[prompt_].draw;
    // Keep global hand card IDs stable; replace only forbidden prompt pairs.
    if (blocker_)
      for (auto& p : players_)
        if (p.seated)
          for (uint8_t j = 0; j < p.size; ++j)
            if (blocker_(blockerContext_, prompt_, p.hand[j])) {
              const uint16_t prior = p.hand[j];
              p.hand[j] = kNoCard;
              const uint16_t fresh = draw();
              p.hand[j] = fresh == kNoCard ? prior : fresh;
            }
    for (uint8_t i = 0; i < kPlayers; ++i)
      if (players_[i].seated) {
        Player& p = players_[i];
        p.submitted = false;
        p.held = false;
        for (auto& slot : p.selected) slot = kNone;
        if (i == czar_) continue;
        for (uint8_t j = 0; j < draw_; ++j) {
          uint16_t c = draw();
          if (c == kNoCard || p.size >= kHandMax) return false;
          p.hand[p.size++] = c;
        }
      }
    if (rules_.rando) {
      Play& bot = pile_[plays_++];
      bot = Play{};
      bot.author = kRando;
      for (uint8_t j = 0; j < pick_; ++j) {
        uint16_t c = draw();
        if (c == kNoCard) return false;
        bot.cards[j] = c;
      }
    }
    return true;
  }
  const Deck* deck_ = nullptr;
  Rules rules_{};
  uint32_t rng_ = 0x9e3779b9u;
  Phase phase_ = Phase::Lobby;
  uint8_t count_ = 0, czar_ = kNone, pick_ = 0, draw_ = 0;
  uint16_t round_ = 0, prompt_ = kNoCard;
  uint8_t plays_ = 0, selected_ = kNone, winner_ = kNone;
  bool aborted_ = false;
  Player players_[kPlayers] = {};
  Play pile_[kPileMax] = {};
  uint16_t enabled_ = 0xffff;
  // Drawn-this-cycle bits, one per card (deck limit 4096): 1 KiB in total.
  static constexpr uint16_t kCardWords = 4096 / 32;
  uint32_t drawnAnswers_[kCardWords] = {};
  uint32_t drawnPrompts_[kCardWords] = {};
  Blocker blocker_ = nullptr;
  void* blockerContext_ = nullptr;
};
}  // namespace cardparty
