// X4 party browser adapter tested on native host with Battleship's fake Wi-Fi.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../../src/apps_local/cardparty/web/CardPartyBrowserServer.h"
#include "../../src/apps_local/cardparty/web/CardPartyProtocol.h"

namespace devmode {
int depth=0;
void pause(){++depth;}
void resume(){--depth;}
}
using namespace cardparty;
using namespace cardparty::web;

struct Fixture {
  char values[130][32]={};
  Answer white[130]={};
  Prompt black[1]={{"Why did production break? ____",1,0}};
  Deck deck{};
  Fixture() {
    for (int i=0;i<130;++i) {
      std::snprintf(values[i],sizeof(values[i]),"Response %d",i);
      white[i].text=values[i];
    }
    deck=Deck{black,1,white,130};
  }
};

std::string lastWelcome(WebSocketsServer& ws, size_t from, int client) {
  const std::string prefix="{\"type\":\"welcome\",\"token\":\"";
  for(size_t i=from;i<ws.messages.size();++i) {
    if(ws.recipients[i]!=client)continue;
    const auto& s=ws.messages[i];
    if(s.rfind(prefix,0)!=0)continue;
    return s.substr(prefix.size(),32);
  }
  return {};
}
bool anyStateFor(WebSocketsServer& ws,int seat) {
  for(size_t i=0;i<ws.messages.size();++i)
    if(ws.recipients[i]==seat && ws.messages[i].find("\"type\":\"state\"")!=std::string::npos)
      return true;
  return false;
}
bool hasPrivateFor(WebSocketsServer& ws, int seat) {
  for(size_t i=0;i<ws.messages.size();++i)
    if(ws.recipients[i]==seat && ws.messages[i].find("\"private\":")!=std::string::npos)
      return true;
  return false;
}
void send(WebSocketsServer& ws, int seat, const std::string& data) {
  ws.event(WStype_TEXT,static_cast<uint8_t>(seat),data);
}
int main() {
  Fixture fixture;
  Game game;
  assert(game.configure(fixture.deck,Rules{5,2,true},11));
  Sessions sessions(game);
  Server server(game,sessions,fixture.deck);
  devmode::pause();
  assert(server.begin(Server::Mode::Hotspot));
  assert(server.running()&&server.hotspot());
  assert(std::strlen(server.password())==16);
  assert(WiFi.currentMode==WIFI_AP);
  assert(devmode::depth==2);
  assert(std::string(server.url()).find("/cardparty")!=std::string::npos);
  auto& ws=*WebSocketsServer::instance;
  // The ESP32 library must enforce Origin during WebSocket upgrade, not
  // merely after CONNECTED when a seat ticket might already be created.
  assert(ws.requiredHeaders.size()==1);
  assert(ws.requiredHeaders[0]=="Origin");
  assert(ws.wouldAcceptOrigin("http://192.168.4.1"));
  assert(ws.wouldAcceptOrigin("http://192.168.4.1:80"));
  assert(!ws.wouldAcceptOrigin(nullptr));
  assert(!ws.wouldAcceptOrigin("https://192.168.4.1"));
  assert(!ws.wouldAcceptOrigin("http://attacker.example"));
  assert(!ws.wouldAcceptOrigin("null"));
  assert(!ws.wouldAcceptOrigin("http://192.168.4.1.evil.example"));
  auto& http=*WebServer::instance;
  assert(http.routes.find("/cardparty")!=http.routes.end());
  http.routes.at("/cardparty")();

  std::string tokens[3];
  for(int i=0;i<3;++i) {
    ws.event(WStype_CONNECTED,static_cast<uint8_t>(i));
    const size_t mark=ws.messages.size();
    send(ws,i,std::string("{\"type\":\"join\",\"name\":\"") + char('A'+i) + "\"}");
    tokens[i]=lastWelcome(ws,mark,i);
    assert(tokens[i].size()==32);
    assert(anyStateFor(ws,i) && hasPrivateFor(ws,i));
    // Invalid proof / roles may not obtain someone else's cards.
    assert(ws.messages.back().find("\"type\":\"state\"")!=std::string::npos);
  }
  assert(game.publicView().phase==Phase::Lobby);
  const size_t marker=ws.messages.size();
  send(ws,3,R"({"type":"join","name":"Not connected"})");
  assert(ws.messages.size()==marker); // unconnected socket is silently ignored
  // Binary frames are dropped rather than interpreted as JSON.
  ws.event(WStype_BIN,0,R"({"type":"join","name":"attacker"})");
  assert(game.publicView().players[3].seated==false);

  assert(server.hostStart());
  const auto initial=game.publicView();
  assert(initial.czar==0 && initial.pick==1);
  const std::string wrong="ffffffffffffffffffffffffffffffff";
  const size_t beforeRejected=ws.messages.size();
  send(ws,1,"{\"type\":\"submit\",\"token\":\""+wrong+"\",\"revision\":0,\"slots\":[0]}");
  assert(ws.messages.size()==beforeRejected+1);
  assert(ws.messages.back().find("\"type\":\"error\"")!=std::string::npos);
  assert(game.publicView().submitted==0);

  send(ws,1,"{\"type\":\"submit\",\"token\":\""+tokens[1]+"\",\"revision\":0,\"slots\":[0]}");
  assert(game.publicView().submitted==1);
  assert(game.publicView().phase==Phase::Playing);
  const size_t beforeReplay=ws.messages.size();
  send(ws,1,"{\"type\":\"submit\",\"token\":\""+tokens[1]+"\",\"revision\":0,\"slots\":[0]}");
  assert(ws.messages.size()==beforeReplay+1);
  assert(game.publicView().submitted==1);

  send(ws,2,"{\"type\":\"submit\",\"token\":\""+tokens[2]+"\",\"revision\":0,\"slots\":[0]}");
  const auto judging=game.publicView();
  assert(judging.phase==Phase::Judging && judging.plays==3); // 2 players + Rando
  for(uint8_t i=0;i<judging.plays;++i)assert(judging.pile[i].author==kNone);
  for(size_t i=0;i<ws.messages.size();++i) {
    const auto& msg=ws.messages[i];
    // No author field ever appears in anonymous judge snapshots.
    if(msg.find("\"phase\":\"Judging\"")!=std::string::npos)
      assert(msg.find("\"author\":")==std::string::npos);
  }
  send(ws,1,"{\"type\":\"judge\",\"token\":\""+tokens[1]+"\",\"revision\":1,\"selected\":0}");
  assert(game.publicView().phase==Phase::Judging);
  send(ws,0,"{\"type\":\"judge\",\"token\":\""+tokens[0]+"\",\"revision\":0,\"selected\":0}");
  assert(game.publicView().phase==Phase::Reveal);
  assert(server.hostNext());
  assert(game.publicView().round==2);

  // Disconnect preserves seat; resume rotates the ticket.
  ws.event(WStype_DISCONNECTED,1);
  assert(!game.publicView().players[1].connected);
  ws.event(WStype_CONNECTED,4);
  const size_t resumeAt=ws.messages.size();
  send(ws,4,"{\"type\":\"resume\",\"token\":\""+tokens[1]+"\"}");
  const std::string renewed=lastWelcome(ws,resumeAt,4);
  assert(renewed.size()==32 && renewed!=tokens[1]);
  assert(game.publicView().players[1].connected);
  send(ws,4,"{\"type\":\"submit\",\"token\":\""+tokens[1]+"\",\"revision\":1,\"slots\":[0]}");
  assert(ws.messages.back().find("\"type\":\"error\"")!=std::string::npos);
  send(ws,1,"{\"type\":\"submit\",\"token\":\""+renewed+"\",\"revision\":1,\"slots\":[0]}");
  assert(ws.messages.back().find("\"type\":\"error\"")!=std::string::npos);

  server.stop();
  assert(!server.running());
  assert(WiFi.currentMode==WIFI_OFF);
  assert(devmode::depth==1);
  server.stop();
  devmode::resume();
  assert(devmode::depth==0);

  // Actual 8-seat transport/score boundary: 7 human plays + Rando = 8.
  // The ninth socket may observe HTTP but must not claim a ninth game seat.
  {
    Game many;
    assert(many.configure(fixture.deck, Rules{5,1,true}, 789));
    Sessions group(many);
    Server eight(many,group,fixture.deck);
    WiFi.mode(WIFI_STA);
    assert(eight.begin(Server::Mode::ExistingWifi));
    auto& bus=*WebSocketsServer::instance;
    assert(bus.wouldAcceptOrigin("http://192.168.4.1"));
    assert(!bus.wouldAcceptOrigin("http://external.example"));
    std::string keys[8];
    for(int i=0;i<8;++i) {
      bus.event(WStype_CONNECTED,static_cast<uint8_t>(i));
      const size_t mark=bus.messages.size();
      send(bus,i,std::string("{\"type\":\"join\",\"name\":\"") + char('A'+i) + "\"}");
      keys[i]=lastWelcome(bus,mark,i);
      assert(keys[i].size()==32);
      assert(hasPrivateFor(bus,i));
    }
    bus.event(WStype_CONNECTED,8);
    const size_t before=bus.messages.size();
    send(bus,8,R"({"type":"join","name":"Ninth"})");
    assert(bus.messages.size()==before+1);
    assert(bus.messages.back().find("\"type\":\"error\"")!=std::string::npos);
    assert(eight.hostStart());
    assert(many.publicView().expected==7);
    for(int i=1;i<8;++i) {
      send(bus,i,"{\"type\":\"submit\",\"token\":\""+keys[i]+"\",\"revision\":0,\"slots\":[0]}");
    }
    const auto view=many.publicView();
    assert(view.phase==Phase::Judging);
    assert(view.plays==8);
    for(uint8_t i=0;i<view.plays;++i)
      assert(view.pile[i].author==kNone);
    assert(bus.messages.back().find("\"private\":")!=std::string::npos);
    send(bus,0,"{\"type\":\"judge\",\"token\":\""+keys[0]+"\",\"revision\":0,\"selected\":7}");
    assert(many.publicView().phase==Phase::Reveal);
    assert(eight.hostNext());
    assert(many.publicView().phase==Phase::Finished);
    eight.stop();
    assert(WiFi.currentMode==WIFI_STA);
    assert(devmode::depth==0);
  }
  std::puts("cardparty X4 server: HTTP, 3-8 seats, auth, anonymous scoring, reconnect and cleanup OK");
}
