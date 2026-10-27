// Cards Against Whatever - host-only rules and secrecy regression suite
#include <cstdio>
#include "CardPartyCore.h"
using namespace cardparty;
static int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;std::printf("FAILED %s:%d %s\n",__FILE__,__LINE__,#x);} } while(0)
struct Fixture {
  char txt[160][24]={};Answer white[160]={};
  Prompt black[3]={{"Server outage: ____.",1,0},{"____ + ____",2,0},{"____, ____ and ____",3,2}};
  Deck deck{};
  Fixture(){for(int i=0;i<160;++i){std::snprintf(txt[i],24,"Example answer %d",i);white[i].text=txt[i];}
    deck=Deck{black,3,white,160};}
};
void validation(){
  Fixture f;Game g;
  CHECK(!g.start());
  CHECK(!g.configure(Deck{},Rules{},5));
  Prompt bad[]={{"bad",0,0}};
  CHECK(!g.configure(Deck{bad,1,f.white,160},Rules{},5));
  CHECK(g.configure(f.deck,Rules{5,2,false},5));
  CHECK(g.join("")==kNone);
  CHECK(g.join("0123456789012345678901234")==kNone);
  auto a=g.join("Alice");CHECK(a==0);CHECK(!g.start());
  auto b=g.join("Bob");CHECK(b==1);CHECK(!g.start());
  CHECK(g.join("Charlie")==2);
  CHECK(g.leaveLobby(1));CHECK(g.join("Bob")==b);
  CHECK(g.start());CHECK(g.join("late")==kNone);
}
void secretAndRound(){
  Fixture f;Game g;CHECK(g.configure(f.deck,Rules{10,2,false},9));
  auto a=g.join("Alice"),b=g.join("Bob"),c=g.join("Carl");
  CHECK(g.start());auto p=g.publicView();
  CHECK(p.czar==a);CHECK(p.expected==2);CHECK(p.plays==0);
  HandView h{};CHECK(g.privateHand(b,h));CHECK(h.size>=10);CHECK(!g.privateHand(150,h));
  const uint8_t slots[]={0,1,2};
  CHECK(!g.submit(a,slots,p.pick));CHECK(!g.submit(b,slots,0));
  CHECK(g.submit(b,slots,p.pick));CHECK(!g.submit(b,slots,p.pick));
  CHECK(g.publicView().plays==0);
  CHECK(g.submit(c,slots,p.pick));
  p=g.publicView();CHECK(p.phase==Phase::Judging);CHECK(p.plays==2);
  for(uint8_t i=0;i<p.plays;++i)CHECK(p.pile[i].author==kNone);
  CHECK(!g.judge(b,0));CHECK(!g.judge(a,2));CHECK(g.judge(a,0));
  p=g.publicView();CHECK(p.phase==Phase::Reveal);CHECK(p.winner==p.pile[0].author);
  CHECK(p.players[p.winner].points==1);
  CHECK(g.nextRound());CHECK(g.publicView().czar==b);
  CHECK(g.submit(a,slots,g.publicView().pick));
  CHECK(g.submit(c,slots,g.publicView().pick));
  CHECK(g.judge(b,1));CHECK(g.nextRound());CHECK(g.phase()==Phase::Finished);
}
void pickAndRando(){
  Fixture f;Game g;CHECK(g.configure(f.deck,Rules{10,0,true},123456));
  uint8_t players[]={g.join("A"),g.join("B"),g.join("C"),g.join("D")};
  CHECK(g.start());bool saw2=false,saw3=false,botWon=false;
  const uint8_t slots[]={0,1,2},duplicate[]={0,0,0};
  for(int turn=0;turn<100;++turn){
    auto v=g.publicView();CHECK(v.phase==Phase::Playing);
    if(v.pick==2)saw2=true;
    if(v.pick==3){saw3=true;CHECK(v.draw==2);}
    for(auto seat:players){
      HandView h{};CHECK(g.privateHand(seat,h));CHECK(h.size<=12);
      for(uint8_t i=0;i<h.size;++i)for(uint8_t j=i+1;j<h.size;++j)CHECK(h.cards[i]!=h.cards[j]);
      if(seat==v.czar)continue;
      if(v.pick>1)CHECK(!g.submit(seat,duplicate,v.pick));
      CHECK(g.submit(seat,slots,v.pick));
    }
    auto j=g.publicView();CHECK(j.phase==Phase::Judging);CHECK(j.plays==4);
    for(uint8_t i=0;i<j.plays;++i){
      CHECK(j.pile[i].author==kNone);
      for(uint8_t x=0;x<v.pick;++x)for(uint8_t y=x+1;y<v.pick;++y)
        CHECK(j.pile[i].cards[x]!=j.pile[i].cards[y]);
    }
    CHECK(g.judge(v.czar,static_cast<uint8_t>(turn%j.plays)));
    auto r=g.publicView();CHECK(r.winner==r.pile[r.selected].author);
    if(r.winner==kRando)botWon=true;
    CHECK(g.nextRound());
  }
  CHECK(saw2);CHECK(saw3);CHECK(botWon);
}
void disconnectAndBounds(){
  Fixture f;Game g;CHECK(g.configure(f.deck,Rules{5,0,false},5));
  auto a=g.join("A"),b=g.join("B"),c=g.join("C");
  CHECK(g.start());const uint8_t slots[]={0,1,2};
  CHECK(!g.closeSubmissions());CHECK(g.submit(b,slots,g.publicView().pick));
  CHECK(g.connected(c,false));CHECK(g.phase()==Phase::Judging);
  CHECK(!g.submit(c,slots,g.publicView().pick));CHECK(g.connected(c,true));
  CHECK(g.judge(a,0));CHECK(g.nextRound());CHECK(g.publicView().czar==b);
  CHECK(g.abortRound());CHECK(g.publicView().aborted);
  CHECK(g.nextRound());CHECK(g.publicView().czar==c);
  Fixture small;small.deck.answersCount=14;Game another;
  CHECK(another.configure(small.deck,Rules{10,0,true},2));
  another.join("A");another.join("B");another.join("C");
  CHECK(!another.start());CHECK(another.phase()==Phase::Lobby);
}
int main(){validation();secretAndRound();pickAndRando();disconnectAndBounds();
 std::printf("cardparty: %d assertions, %d failures\n",checks,failures);
 return failures?1:0;}
