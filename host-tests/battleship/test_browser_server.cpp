#include <cassert>
#include <cstdio>

#include "../../src/apps_local/battleship/web/BattleshipBrowserServer.h"
namespace devmode {
int depth = 0;
void pause() { ++depth; }
void resume() { --depth; }
}  // namespace devmode
struct Handler {
  int calls = 0, disconnects = 0;
  bshipweb::PlacementView view;
};
int main() {
  using namespace bshipweb;
  Server server;
  Handler handler;
  server.setCommands(
      &handler,
      [](void* p, const Command&, PlacementView& v) {
        auto& h = *static_cast<Handler*>(p);
        ++h.calls;
        h.view.profile = true;
        ++h.view.revision;
        v = h.view;
        return true;
      },
      [](void* p) { ++static_cast<Handler*>(p)->disconnects; });
  devmode::pause();  // activity's outer ownership
  assert(server.begin(Server::NetworkMode::Hotspot));
  assert(devmode::depth == 2 && !server.clientSeen());
  auto& ws = *WebSocketsServer::instance;
  auto& http = *WebServer::instance;
  http.routes.at("/battleship/session")();
  const auto token = http.response;
  assert(token.size() == 32);
  const auto profile = "[\"profile\",\"" + token + "\",0,0,13,0]";
  ws.event(WStype_CONNECTED, 0);
  assert(server.clientSeen() && ws.messages.size() == 1);
  BrowserSnapshot state{BrowserPhase::Playing, true, true};
  server.publish(state);
  const auto count = ws.messages.size();
  server.publish(state);
  assert(ws.messages.size() == count);
  ws.event(WStype_BIN, 0, profile);
  ws.event(WStype_FRAGMENT_TEXT_START, 0, profile);
  assert(ws.messages.size() == count && handler.calls == 0);
  ws.event(WStype_TEXT, 0, "invalid");
  assert(handler.calls == 0);
  ws.event(WStype_TEXT, 0, "[\"ready\",\"" + token + "\",0]");
  assert(handler.calls == 0);
  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 1 && ws.recipients.back() == 0);
  assert(ws.messages.back().find("placement") != std::string::npos);
  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 1);  // bounded application command rate
  clockMs += 100;
  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 2);
  ws.event(WStype_CONNECTED, 1);
  assert(ws.messages.back().find("playing") != std::string::npos);
  assert(ws.messages.back().find("ships") == std::string::npos);
  ws.event(WStype_TEXT, 1, profile);
  assert(handler.calls == 2 && ws.messages.back().find("error") != std::string::npos);
  ws.event(WStype_DISCONNECTED, 1);
  assert(server.clientSeen() && handler.disconnects == 0);
  ws.event(WStype_DISCONNECTED, 0);
  assert(!server.clientSeen() && handler.disconnects == 1);
  ws.event(WStype_CONNECTED, 0);
  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 2);  // reused socket index, old capability
  http.routes.at("/battleship/session")();
  assert(http.response != token);
  server.stop();
  server.stop();
  assert(ws.closed && !server.running() && devmode::depth == 1 && WiFi.currentMode == WIFI_OFF);
  WiFi.available = false;
  assert(!server.begin(Server::NetworkMode::ExistingWifi));
  assert(devmode::depth == 1);
  assert(!server.begin(Server::NetworkMode::Hotspot));
  assert(devmode::depth == 1 && WiFi.currentMode == WIFI_OFF);
  WiFi.available = true;
  WiFi.mode(WIFI_STA);
  assert(server.begin(Server::NetworkMode::ExistingWifi));
  assert(!server.clientSeen());
  server.stop();
  assert(devmode::depth == 1 && WiFi.currentMode == WIFI_STA);
  devmode::resume();
  assert(devmode::depth == 0);
  puts("Browser server: ownership, capability, private replies, reconnect, AP/LAN and nested radio yield passed");
}
