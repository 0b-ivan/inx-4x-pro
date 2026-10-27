#pragma once
// Cards Against Whatever: deterministic, freestanding rules core.
//
// No storage, HTTP, authentication or UI dependencies. The transport must
// authenticate each seat and NEVER serialize internal Game state.
#include <cstdint>
#include <cstring>

namespace cardparty {
constexpr uint8_t kPlayers=8, kHandMax=12, kPickMax=3, kPileMax=8;
constexpr uint8_t kNone=255, kRando=254;
constexpr uint16_t kNoCard=65535;
enum class Phase : uint8_t { Lobby, Playing, Judging, Reveal, Finished };
struct Prompt { const char* text; uint8_t pick, draw; };
struct Answer { const char* text; };
struct Deck {
  const Prompt* prompts=nullptr; uint16_t promptsCount=0;
  const Answer* answers=nullptr; uint16_t answersCount=0;
};
struct Rules { uint8_t hand=10, rounds=10; bool rando=false; };
struct PublicPlayer { char name[24]={}; bool seated=false, connected=false; uint16_t points=0; };
struct PublicPlay {
  uint16_t cards[kPickMax]={kNoCard,kNoCard,kNoCard};
  uint8_t author=kNone; // never populated before Reveal
};
struct PublicView {
  Phase phase=Phase::Lobby; uint8_t czar=kNone, pick=0, draw=0;
  uint16_t round=0, prompt=kNoCard;
  uint8_t submitted=0, expected=0, plays=0, selected=kNone, winner=kNone;
  bool aborted=false;
  PublicPlayer players[kPlayers]={};
  PublicPlay pile[kPileMax]={};
};
struct HandView {
  uint8_t size=0; uint16_t cards[kHandMax]={};
  bool submitted=false, judge=false;
};

class Game {
 public:
  bool configure(const Deck& deck, Rules rules, uint32_t seed) {
    if (!deck.prompts || !deck.answers || !deck.promptsCount || !deck.answersCount ||
        deck.promptsCount>4096 || deck.answersCount>4096 ||
        rules.hand<3 || rules.hand>10) return false;
    for (uint16_t i=0;i<deck.promptsCount;++i) {
      const Prompt& p=deck.prompts[i];
      if (!p.text || !p.text[0] || p.pick<1 || p.pick>3 ||
          p.draw>2 || rules.hand+p.draw>kHandMax ||
          p.pick>rules.hand+p.draw) return false;
    }
    for (uint16_t i=0;i<deck.answersCount;++i)
      if (!deck.answers[i].text || !deck.answers[i].text[0]) return false;
    deck_=&deck; rules_=rules; rng_=seed?seed:0x9e3779b9u;
    phase_=Phase::Lobby; count_=0; round_=0; prompt_=kNoCard;
    czar_=kNone; plays_=0; selected_=kNone; winner_=kNone; aborted_=false;
    for (auto& player:players_) player=Player{};
    for (auto& play:pile_) play=Play{};
    return true;
  }
  uint8_t join(const char* name) {
    if (phase_!=Phase::Lobby || !deck_ || !name) return kNone;
    size_t n=0; while (name[n] && n<=23) ++n;
    if (!n || n>23) return kNone;
    for (uint8_t i=0;i<kPlayers;++i) if (!players_[i].seated) {
      players_[i]=Player{}; Player& p=players_[i];
      std::memcpy(p.name,name,n); p.name[n]=0;
      p.seated=p.connected=true; ++count_; return i;
    }
    return kNone;
  }
  bool leaveLobby(uint8_t seat) {
    if (phase_!=Phase::Lobby || !valid(seat)) return false;
    players_[seat]=Player{}; --count_; return true;
  }
  bool connected(uint8_t seat, bool value) {
    if (!valid(seat)) return false;
    players_[seat].connected=value;
    if (phase_==Phase::Playing) maybeJudge();
    return true;
  }
  bool start() {
    if (phase_!=Phase::Lobby || active()<3) return false;
    uint8_t maxDraw=0;
    for (uint16_t i=0;i<deck_->promptsCount;++i)
      if (deck_->prompts[i].draw>maxDraw) maxDraw=deck_->prompts[i].draw;
    // Enough distinct cards for all hands, special draw and bot.
    if (deck_->answersCount <
        count_*rules_.hand+(count_-1)*maxDraw+(rules_.rando?3:0)) return false;
    for (auto& p:players_) if (p.seated)
      while (p.size<rules_.hand) { const uint16_t c=draw(); if(c==kNoCard)return false; p.hand[p.size++]=c; }
    czar_=next(kNone); round_=1; return beginRound();
  }
  bool submit(uint8_t seat, const uint8_t* slots, uint8_t n) {
    if (phase_!=Phase::Playing || !valid(seat) || !players_[seat].connected ||
        seat==czar_ || !slots || n!=pick_ || players_[seat].submitted ||
        plays_>=kPileMax) return false;
    Player& p=players_[seat];
    for (uint8_t i=0;i<n;++i) {
      if (slots[i]>=p.size) return false;
      for (uint8_t j=0;j<i;++j) if (slots[j]==slots[i]) return false;
    }
    Play& play=pile_[plays_++]; play=Play{}; play.author=seat;
    p.submitted=true;
    for (uint8_t i=0;i<n;++i) {p.selected[i]=slots[i];play.cards[i]=p.hand[slots[i]];}
    maybeJudge(); return true;
  }
  // Host-only action for timeout/disconnect. Never expose unguarded to clients.
  bool closeSubmissions() {
    if (phase_!=Phase::Playing || humanPlays()==0) return false;
    shuffle(); phase_=Phase::Judging; return true;
  }
  bool judge(uint8_t seat,uint8_t selected) {
    if (phase_!=Phase::Judging || seat!=czar_ || !valid(seat) ||
        !players_[seat].connected || selected>=plays_) return false;
    selected_=selected; winner_=pile_[selected].author;
    if(winner_<kPlayers) ++players_[winner_].points;
    phase_=Phase::Reveal; return true;
  }
  bool abortRound() {
    if (phase_!=Phase::Playing && phase_!=Phase::Judging) return false;
    phase_=Phase::Reveal; winner_=kNone; selected_=kNone; aborted_=true; return true;
  }
  bool nextRound() {
    if (phase_!=Phase::Reveal) return false;
    if (rules_.rounds && round_>=rules_.rounds) {phase_=Phase::Finished;return true;}
    if (active()<3) return false;
    for (auto& p:players_) if (p.seated) {
      uint8_t w=0;
      for (uint8_t i=0;i<p.size;++i) {
        bool played=false;
        if(p.submitted) for(uint8_t j=0;j<pick_;++j)
          if(p.selected[j]==i) played=true;
        if(!played) p.hand[w++]=p.hand[i];
      }
      // Unplayed Draw-Two extras expire after the current round.
      if(w>rules_.hand) w=rules_.hand;
      p.size=w; p.submitted=false;
      for(auto& slot:p.selected)slot=kNone;
    }
    for (auto& p:players_) if(p.seated)
      while(p.size<rules_.hand){const uint16_t c=draw();if(c==kNoCard)return false;p.hand[p.size++]=c;}
    czar_=next(czar_); ++round_; return beginRound();
  }
  PublicView publicView() const {
    PublicView v{};v.phase=phase_;v.czar=czar_;v.round=round_;v.prompt=prompt_;
    v.pick=pick_;v.draw=draw_;v.submitted=humanPlays();v.expected=expected();
    for(uint8_t i=0;i<kPlayers;++i) {
      const Player& p=players_[i];auto& q=v.players[i];
      q.seated=p.seated;q.connected=p.connected;q.points=p.points;
      std::memcpy(q.name,p.name,sizeof(q.name));
    }
    if(phase_==Phase::Judging || phase_==Phase::Reveal || phase_==Phase::Finished) {
      v.plays=plays_;
      for(uint8_t i=0;i<plays_;++i) {
        for(uint8_t j=0;j<pick_;++j)v.pile[i].cards[j]=pile_[i].cards[j];
        // Never reveal author in judgment stage, including Rando identity.
        if(phase_!=Phase::Judging)v.pile[i].author=pile_[i].author;
      }
    }
    if(phase_==Phase::Reveal || phase_==Phase::Finished) {
      v.winner=winner_;v.selected=selected_;v.aborted=aborted_;
    }
    return v;
  }
  // Server must check the requesting client's authenticated seat first.
  bool privateHand(uint8_t seat,HandView& out) const {
    if(!valid(seat))return false;
    out=HandView{};const Player& p=players_[seat];
    out.size=p.size;out.submitted=p.submitted;out.judge=(seat==czar_);
    for(uint8_t i=0;i<p.size;++i)out.cards[i]=p.hand[i];
    return true;
  }
  Phase phase() const{return phase_;}
 private:
  struct Player {
    bool seated=false,connected=false,submitted=false; char name[24]={};
    uint16_t hand[kHandMax]={}; uint8_t size=0;
    uint8_t selected[kPickMax]={kNone,kNone,kNone};uint16_t points=0;
  };
  struct Play {uint8_t author=kNone;uint16_t cards[kPickMax]={kNoCard,kNoCard,kNoCard};};
  bool valid(uint8_t seat) const {return seat<kPlayers && players_[seat].seated;}
  uint8_t active() const {uint8_t n=0;for(const auto& p:players_)if(p.seated&&p.connected)++n;return n;}
  uint8_t next(uint8_t prev) const {
    for(uint8_t d=1;d<=kPlayers;++d) {
      const uint8_t i=static_cast<uint8_t>((prev+d)%kPlayers);
      if(players_[i].seated && players_[i].connected)return i;
    }
    return kNone;
  }
  uint32_t random() {rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;return rng_;}
  uint16_t draw() {
    const uint16_t n=deck_->answersCount;
    const uint16_t offset=static_cast<uint16_t>(random()%n);
    for(uint16_t step=0;step<n;++step) {
      const uint16_t candidate=static_cast<uint16_t>((offset+step)%n);
      bool used=false;
      for(const auto& p:players_)if(p.seated)
        for(uint8_t j=0;j<p.size;++j)if(p.hand[j]==candidate)used=true;
      for(uint8_t i=0;i<plays_;++i)if(pile_[i].author==kRando)
        for(uint8_t j=0;j<kPickMax;++j)if(pile_[i].cards[j]==candidate)used=true;
      if(!used)return candidate;
    }
    return kNoCard;
  }
  uint8_t expected() const {
    // A disconnected player whose cards are already in the pile must
    // *still* count toward the round's expected answer total. Otherwise
    // dropping the Wi-Fi link after submitting could prematurely advance
    // to Judging while another connected player has not yet answered.
    // Players who left before submitting no longer block the round.
    uint8_t n=0;
    for(uint8_t i=0;i<kPlayers;++i)
      if(i!=czar_ && players_[i].seated &&
         (players_[i].connected || players_[i].submitted)) ++n;
    return n;
  }
  uint8_t humanPlays() const {uint8_t n=0;for(uint8_t i=0;i<plays_;++i)if(pile_[i].author!=kRando)++n;return n;}
  void shuffle() {
    for(uint8_t i=plays_;i>1;--i) {
      const uint8_t j=static_cast<uint8_t>(random()%i);
      const Play x=pile_[i-1];pile_[i-1]=pile_[j];pile_[j]=x;
    }
  }
  void maybeJudge() {
    if(phase_==Phase::Playing&&humanPlays()>0&&humanPlays()>=expected()) {
      shuffle();phase_=Phase::Judging;
    }
  }
  bool beginRound() {
    phase_=Phase::Playing;plays_=0;selected_=kNone;winner_=kNone;aborted_=false;
    const uint16_t n=static_cast<uint16_t>(random()%deck_->promptsCount);
    prompt_=(n==prompt_&&deck_->promptsCount>1)?static_cast<uint16_t>((n+1)%deck_->promptsCount):n;
    pick_=deck_->prompts[prompt_].pick;draw_=deck_->prompts[prompt_].draw;
    for(uint8_t i=0;i<kPlayers;++i)if(players_[i].seated) {
      Player& p=players_[i];p.submitted=false;for(auto& slot:p.selected)slot=kNone;
      if(i==czar_)continue;
      for(uint8_t j=0;j<draw_;++j){uint16_t c=draw();if(c==kNoCard||p.size>=kHandMax)return false;p.hand[p.size++]=c;}
    }
    if(rules_.rando) {
      Play& bot=pile_[plays_++];bot=Play{};bot.author=kRando;
      for(uint8_t j=0;j<pick_;++j){uint16_t c=draw();if(c==kNoCard)return false;bot.cards[j]=c;}
    }
    return true;
  }
  const Deck* deck_=nullptr; Rules rules_{};uint32_t rng_=0x9e3779b9u;
  Phase phase_=Phase::Lobby;uint8_t count_=0,czar_=kNone,pick_=0,draw_=0;
  uint16_t round_=0,prompt_=kNoCard;uint8_t plays_=0,selected_=kNone,winner_=kNone;
  bool aborted_=false;Player players_[kPlayers]={};Play pile_[kPileMax]={};
};
} // namespace cardparty
