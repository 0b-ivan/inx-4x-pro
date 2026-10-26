#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../src/apps_local/connectfour/web/ConnectFourBrowserServer.h"

namespace devmode {
int depth=0;
void pause(){ ++depth; }
void resume(){ --depth; }
}  // namespace devmode

int main() {
  c4web::Server server;
  devmode::pause();
  assert(server.begin());
  assert(devmode::depth==2);
  assert(server.running() && !server.clientSeen());
  assert(WiFi.apPassword.empty()); // Passwordless game AP.
  assert(std::string(server.url())=="http://192.168.4.1/connectfour");
  auto& ws=*WebSocketsServer::instance;
  auto& http=*WebServer::instance;
  http.routes.at("/connectfour/session")();
  const std::string token=http.response;
  assert(token.size()==32 && !server.round().joined());
  ws.event(WStype_CONNECTED,0);
  assert(server.clientSeen());
  assert(ws.messages.size()==1 && ws.messages.back().find("\"phase\":\"waiting\"")!=std::string::npos);
  const std::string join="[\"join\",\""+token+"\",0]";
  ws.event(WStype_BIN,0,join);
  assert(!server.round().joined());
  ws.event(WStype_TEXT,0,join);
  assert(server.round().joined() && server.round().owner()==0);
  assert(ws.messages[ws.messages.size()-2].find("\"type\":\"resume\"")!=std::string::npos);
  assert(ws.recipients[ws.recipients.size()-2]==0);
  const std::string secret=server.round().resumeToken();
  assert(ws.messages.back().find("\"phase\":\"playing\"")!=std::string::npos);
  assert(ws.messages.back().find(secret)==std::string::npos);
  ws.event(WStype_CONNECTED,1);
  assert(ws.messages.back().find(secret)==std::string::npos);
  clockMs=250;
  ws.event(WStype_TEXT,1,"[\"drop\",\""+token+"\",0,4]");
  assert(ws.messages.back().find("error")!=std::string::npos);
  assert(!server.deviceRematch());
  assert(server.deviceDrop(3));
  assert(server.round().game().turn==connectfour::kDark);
  clockMs=350;
  const std::string move="[\"drop\",\""+token+"\",1,4]";
  ws.event(WStype_TEXT,0,move);
  assert(server.round().game().cell[4][0]==connectfour::kDark);
  assert(ws.recipients.back()==-1);
  assert(ws.messages.back().find("\"r\":2")!=std::string::npos);
  ws.event(WStype_TEXT,0,move);
  assert(ws.messages.back().find("error")!=std::string::npos);
  assert(server.round().revision()==2);
  ws.event(WStype_DISCONNECTED,0);
  assert(server.clientSeen() && !server.round().hasOwner());
  http.routes.at("/connectfour/session")();
  const std::string fresh=http.response;
  assert(fresh!=token);
  ws.event(WStype_CONNECTED,2);
  assert(ws.messages.back().find(secret)==std::string::npos);
  clockMs=550;
  const std::string resume="[\"resume\",\""+fresh+"\",1,\""+secret+"\"]";
  ws.event(WStype_TEXT,2,resume);
  assert(server.round().hasOwner() && server.round().owner()==2);
  assert(ws.messages[ws.messages.size()-2].find("\"type\":\"resume\"")!=std::string::npos);
  assert(ws.recipients[ws.recipients.size()-2]==2);
  assert(ws.messages.back().find("\"r\":2")!=std::string::npos);
  assert(server.round().authorized(fresh.c_str(),secret.c_str()));
  // Browser reload: a new socket resumes before the old socket closes.
  ws.event(WStype_CONNECTED,3);
  clockMs=650;
  ws.event(WStype_TEXT,3,"[\\\"resume\\\",\\\""+fresh+"\\\",0,\\\""+secret+"\\\"]");
  assert(server.round().owner()==3);
  assert(ws.recipients[ws.recipients.size()-2]==3);
  ws.event(WStype_DISCONNECTED,2);
  assert(server.round().owner()==3);
  // End a complete game with four vertical X4 discs, then replay from the
  // physical result screen. The browser must receive the fresh board and
  // revision, not remain stuck on the prior winner.
  for (int i=0; i<2; ++i) {
    assert(server.deviceDrop(3));
    clockMs=static_cast<uint32_t>(700+i*200);
    const std::string moveAgain=std::string(R"(["drop",")")+fresh+R"(",)"+std::to_string(server.round().revision())+",4]";
    ws.event(WStype_TEXT,3,moveAgain);
    assert(server.round().game().turn==connectfour::kLight);
  }
  assert(server.deviceDrop(3));
  assert(server.round().game().outcome==connectfour::Outcome::LightWins);
  assert(server.deviceRematch());
  assert(server.round().game().outcome==connectfour::Outcome::Running);
  assert(connectfour::discCount(server.round().game())==0);
  assert(ws.messages.back().find(R"("r":8)")!=std::string::npos);
  server.stop();server.stop();
  assert(!server.running() && WiFi.currentMode==WIFI_OFF);
  assert(devmode::depth==1);
  assert(!server.round().authorized(fresh.c_str(),secret.c_str()));
  WiFi.available=false;
  assert(!server.begin());
  assert(!server.running() && WiFi.currentMode==WIFI_OFF);
  WiFi.available=true;
  assert(devmode::depth==1);
  devmode::resume();
  assert(devmode::depth==0);
  puts("Connect Four server: open hotspot, owner-only resume, guarded WS commands and reconnect passed");
}
