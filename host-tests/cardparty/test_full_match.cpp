// Full offline match regression against the actual German demo deck.
// The native host simulates users, not real Wi-Fi / devices.
#include <cassert>
#include <cstdio>
#include "CardPartyCore.h"
#include "CardPartyDemoDeck.h"
using namespace cardparty;

void runMatch(uint8_t count,uint32_t seed) {
  Game game;
  const auto& deck=demoDeck();
  assert(game.configure(deck, Rules{5,10,true},seed));
  uint8_t seats[kPlayers]={};
  for(uint8_t i=0;i<count;++i) {
    char label[16];std::snprintf(label,sizeof(label),"Tester-%d",i);
    seats[i]=game.join(label);
    assert(seats[i]==i);
  }
  assert(game.start());
  int humansWon=0,botWon=0;
  int pick3=0,pick2=0,draw2=0;
  for(uint8_t round=1;round<=10;++round) {
    const PublicView initial=game.publicView();
    assert(initial.phase==Phase::Playing);
    assert(initial.round==round);
    assert(initial.czar==seats[(round-1)%count]);
    assert(initial.plays==0);
    assert(initial.expected==count-1);
    if(initial.pick==2)++pick2;
    if(initial.pick==3)++pick3;
    if(initial.draw==2)++draw2;
    uint8_t slots[3]={0,1,2};
    for(uint8_t i=0;i<count;++i) {
      HandView hand{};
      assert(game.privateHand(i,hand));
      assert(hand.size<=kHandMax);
      if(i==initial.czar)continue;
      assert(hand.size>=initial.pick);
      assert(game.submit(i,slots,initial.pick));
      assert(!game.submit(i,slots,initial.pick));
      const auto mid=game.publicView();
      if(mid.phase==Phase::Playing) assert(mid.plays==0);
    }
    const auto judging=game.publicView();
    assert(judging.phase==Phase::Judging);
    assert(judging.plays==count); // n-1 humans plus the anonymous bot
    assert(judging.submitted==count-1);
    for(uint8_t i=0;i<judging.plays;++i) {
      assert(judging.pile[i].author==kNone);
      for(uint8_t a=0;a<initial.pick;++a) {
        assert(judging.pile[i].cards[a]<deck.answersCount);
        for(uint8_t b=a+1;b<initial.pick;++b)
          assert(judging.pile[i].cards[a]!=judging.pile[i].cards[b]);
      }
    }
    // Always choose the first *shuffled* anonymous response.
    assert(game.judge(initial.czar,0));
    const auto reveal=game.publicView();
    assert(reveal.phase==Phase::Reveal);
    assert(reveal.winner==reveal.pile[reveal.selected].author);
    if(reveal.winner==kRando) ++botWon;
    else ++humansWon;
    assert(!game.judge(initial.czar,0)); // replay protection
    int points=0;
    for(uint8_t i=0;i<count;++i)points+=reveal.players[i].points;
    assert(points==humansWon);
    assert(game.nextRound());
  }
  assert(game.phase()==Phase::Finished);
  assert(game.round()==10);
  assert(!game.nextRound());
  std::printf("PASS demo 10 rounds, %d humans, seed=%u, pick2=%d, pick3=%d, draw2=%d, bot wins=%d\n",
      count, seed, pick2,pick3,draw2,botWon);
}
int main() {
  // Three-player real-world baseline, maximum-eight-player capacity, plus
  // additional seeds to avoid relying on one predetermined card shuffle.
  for(uint8_t n: {uint8_t(3),uint8_t(8)})
    for(uint32_t seed: {uint32_t(1),uint32_t(89),uint32_t(20261027)})
      runMatch(n,seed);
  std::puts("PASS CardParty full demo-game simulator scenarios");
}
