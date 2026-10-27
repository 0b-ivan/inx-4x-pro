// CardParty lost-connection regressions. A submitted offline player must
// not cause the remaining active submitters to lose their turn.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "CardPartyCore.h"
#include "CardPartySessions.h"
using namespace cardparty;

namespace {
struct Fixture {
  char names[110][24] = {};
  Answer answers[110] = {};
  Prompt prompts[3] = {
    {"Whose fault is ____?", 1, 0},
    {"____ and ____", 2, 0},
    {"____, ____ and ____", 3, 2}
  };
  Deck deck{};
  Fixture() {
    for(int i=0;i<110;++i) {
      std::snprintf(names[i],sizeof names[i],"Card %d",i);
      answers[i].text=names[i];
    }
    deck=Deck{prompts,3,answers,110};
  }
};
struct Key {
  uint8_t bytes[kTicketBytes]={};
  explicit Key(uint8_t base) {
    for(size_t i=0;i<kTicketBytes;++i) bytes[i]=static_cast<uint8_t>(base+i+1);
  }
};
void submittedPlayerDisconnectMustNotSkipOtherPlayer() {
  Fixture f;
  Game g;
  assert(g.configure(f.deck,Rules{5,3,true},0x137acafe));
  Sessions sessions(g);
  const Key a(10), b(40), c(70), fresh(100), rogue(160);
  SessionTicket ta{},tb{},tc{},renewed{};
  assert(sessions.join(10,"Judge",a.bytes,ta));
  assert(sessions.join(11,"Alice",b.bytes,tb));
  assert(sessions.join(12,"Bob",c.bytes,tc));
  assert(sessions.hostStart());
  const auto initial=g.publicView();
  assert(initial.phase==Phase::Playing && initial.expected==2 && initial.czar==ta.seat);

  const uint8_t slots[]={0,1,2};
  assert(sessions.submit(11,b.bytes,0,slots,initial.pick));
  assert(g.publicView().submitted==1);
  // Before the fix, this disconnect decremented expected to 1 and
  // promoted phase to Judging although Bob was still selecting his hand.
  assert(sessions.disconnect(11));
  const auto interrupted=g.publicView();
  assert(interrupted.phase==Phase::Playing);
  assert(interrupted.submitted==1);
  assert(interrupted.expected==2);
  assert(interrupted.players[tb.seat].connected==false);
  assert(!sessions.submit(11,b.bytes,1,slots,initial.pick));
  assert(sessions.submit(12,c.bytes,0,slots,initial.pick));
  const auto judging=g.publicView();
  assert(judging.phase==Phase::Judging);
  assert(judging.submitted==2 && judging.expected==2);
  assert(judging.plays==3); // Alice + Bob + Rando
  for(uint8_t i=0;i<judging.plays;++i)
    assert(judging.pile[i].author==kNone);

  // A disconnected Card Czar may not choose a winner from a stale socket
  // or the old token. Resume preserves the seat but rotates the secret.
  assert(sessions.disconnect(10));
  assert(!sessions.judge(10,a.bytes,0,0));
  assert(!sessions.resume(13,rogue.bytes,fresh.bytes,renewed));
  assert(sessions.resume(13,a.bytes,fresh.bytes,renewed));
  assert(renewed.seat==ta.seat);
  assert(!sessions.judge(13,a.bytes,0,0));
  assert(sessions.judge(13,fresh.bytes,0,0));
  assert(g.publicView().phase==Phase::Reveal);
  assert(!sessions.judge(13,fresh.bytes,0,0));
  // Since only two human seats are online, nextRound must be rejected
  // until the previously submitted player returns.
  assert(!sessions.hostNext());
  const Key freshB(130);
  SessionTicket resumedB{};
  assert(sessions.resume(14,b.bytes,freshB.bytes,resumedB));
  assert(sessions.hostNext());
  assert(g.publicView().phase==Phase::Playing);
  assert(g.publicView().round==2);
}
void unsubmittedDisconnectMayFinishRound() {
  Fixture f;
  Game g;
  assert(g.configure(f.deck,Rules{5,2,true},0x1011));
  Sessions s(g);
  const Key a(1),b(22),c(43);
  SessionTicket ta{},tb{},tc{};
  assert(s.join(0,"Judge",a.bytes,ta));
  assert(s.join(1,"Alice",b.bytes,tb));
  assert(s.join(2,"Bob",c.bytes,tc));
  assert(s.hostStart());
  // Bob is disconnected BEFORE submitting. His turn is held (screen lock,
  // app switch) so Alice's answer alone does not skip him...
  assert(s.disconnect(2));
  const auto v=g.publicView();
  assert(v.phase==Phase::Playing && v.expected==2 && g.held(tc.seat));
  const uint8_t slots[]={0,1,2};
  assert(s.submit(1,b.bytes,0,slots,v.pick));
  assert(g.publicView().phase==Phase::Playing);
  // ...until the transport's grace window releases it; then the round
  // advances instead of hanging forever on a player who disappeared.
  assert(g.releaseTurn(tc.seat));
  assert(!g.releaseTurn(tc.seat));
  const auto judging=g.publicView();
  assert(judging.phase==Phase::Judging && judging.expected==1);
  assert(judging.submitted==1 && judging.plays==2);
  assert(s.judge(0,a.bytes,0,0));
  assert(g.publicView().phase==Phase::Reveal);
  // Finish can be recovered after returning offline player.
  assert(!s.hostNext());
  const Key fresh(80);
  SessionTicket resume{};
  assert(s.resume(3,c.bytes,fresh.bytes,resume));
  assert(s.hostNext());
}
void multipleSubmittedDisconnectsStillWaitForRemainingSeats() {
  Fixture f;
  Game g;
  assert(g.configure(f.deck,Rules{5,1,true},0x303));
  for(int i=0;i<8;++i) {
    char name[16];std::snprintf(name,sizeof name,"Player %d",i);
    assert(g.join(name)==i);
  }
  assert(g.start());
  const auto start=g.publicView();
  const uint8_t slots[]={0,1,2};
  // Six players have answered; all but the still-pending seventh disconnect.
  for(uint8_t i=1;i<7;++i) {
    assert(g.submit(i,slots,start.pick));
    assert(g.connected(i,false));
    const auto v=g.publicView();
    assert(v.phase==Phase::Playing);
    assert(v.submitted==i);
    assert(v.expected==7);
  }
  assert(g.submit(7,slots,start.pick));
  const auto v=g.publicView();
  assert(v.phase==Phase::Judging);
  assert(v.expected==7 && v.submitted==7 && v.plays==8);
  assert(g.judge(0,0));
  assert(g.nextRound() && g.phase()==Phase::Finished);
}
void heldTurnReturnsBeforeGrace() {
  Fixture f;
  Game g;
  assert(g.configure(f.deck,Rules{5,2,true},0x2024));
  for(const char* n:{"Czar","Alice","Bob"}) assert(g.join(n)!=kNone);
  assert(g.start());
  const auto v=g.publicView();
  const uint8_t alice=static_cast<uint8_t>((v.czar+1)%3), bob=static_cast<uint8_t>((v.czar+2)%3);
  const uint8_t slots[]={0,1,2};
  assert(g.submit(alice,slots,v.pick));
  // Bob locks his phone: held, round waits.
  assert(g.connected(bob,false) && g.held(bob));
  assert(g.phase()==Phase::Playing && g.publicView().expected==2);
  // He returns within the grace window and answers normally.
  assert(g.connected(bob,true) && !g.held(bob));
  assert(g.submit(bob,slots,v.pick));
  assert(g.phase()==Phase::Judging && g.publicView().plays==3);
  // The Czar and players who already answered are never held.
  assert(g.connected(alice,false) && !g.held(alice));
  assert(g.connected(v.czar,false) && !g.held(v.czar));
  // The host may still close a stalled round at any time.
  Game h;
  assert(h.configure(f.deck,Rules{5,2,true},0x2025));
  for(const char* n:{"Czar","Alice","Bob"}) assert(h.join(n)!=kNone);
  assert(h.start());
  const auto w=h.publicView();
  const uint8_t a2=static_cast<uint8_t>((w.czar+1)%3), b2=static_cast<uint8_t>((w.czar+2)%3);
  assert(h.submit(a2,slots,w.pick));
  assert(h.connected(b2,false) && h.held(b2));
  assert(h.closeSubmissions() && h.phase()==Phase::Judging);
}
} // namespace
int main() {
  submittedPlayerDisconnectMustNotSkipOtherPlayer();
  unsubmittedDisconnectMayFinishRound();
  multipleSubmittedDisconnectsStillWaitForRemainingSeats();
  heldTurnReturnsBeforeGrace();
  std::puts("cardparty disconnect regression: submitted/unsent/held-turn/Czar/8-seat transitions PASS");
}
