#include <cassert>
#include <cstdio>

#include "../../src/apps_local/battleship/web/BattleshipBrowserServer.h"
namespace devmode {
int depth = 0;
void pause() { ++depth; }
void resume() { --depth; }
}  // namespace devmode
int main() {
  using namespace bshipweb;
  Server server;
  assert(server.begin(Server::NetworkMode::Hotspot));
  assert(devmode::depth == 1 && !server.clientSeen());
  auto& ws = *WebSocketsServer::instance;
  ws.event(WStype_CONNECTED);
  assert(server.clientSeen() && ws.messages.size() == 1);
  BrowserSnapshot state{BrowserPhase::Playing, true, true};
  server.publish(state);
  const auto count = ws.messages.size();
  server.publish(state);
  assert(ws.messages.size() == count);
  ws.event(WStype_TEXT);
  ws.event(WStype_BIN);
  ws.event(WStype_FRAGMENT_TEXT_START);
  assert(ws.messages.size() == count);
  ws.event(WStype_CONNECTED);
  assert(ws.messages.size() == count + 1 && ws.messages.back().find("playing") != std::string::npos);
  ws.event(WStype_DISCONNECTED);
  assert(server.clientSeen());
  ws.event(WStype_DISCONNECTED);
  assert(!server.clientSeen());
  server.stop();
  server.stop();
  assert(ws.closed && !server.running() && devmode::depth == 0 && WiFi.currentMode == WIFI_OFF);
  WiFi.available = false;
  assert(!server.begin(Server::NetworkMode::ExistingWifi));
  assert(devmode::depth == 0);
  WiFi.available = true;
  assert(server.begin(Server::NetworkMode::ExistingWifi));
  assert(!server.clientSeen());
  server.stop();
  assert(devmode::depth == 0);
  puts("Browser server: connect, change-only push, read-only input, multiple clients, cleanup passed");
}
