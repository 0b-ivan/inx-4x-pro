#include <cassert>
#include <cstdio>
#include <string>

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
  server.setOpponentName("SPIKY WINK BEARD");
  server.setCommands(
      &handler,
      [](void* p, const Command& command, PlacementView& v) {
        auto& h = *static_cast<Handler*>(p);
        ++h.calls;
        if (command.kind == CommandKind::Profile) {
          h.view.profile = true;
          h.view.ready = true;
          ++h.view.revision;
        } else if (command.kind != CommandKind::Resume) {
          ++h.view.revision;
        }
        v = h.view;
        return true;
      },
      [](void* p) { ++static_cast<Handler*>(p)->disconnects; });
  devmode::pause();
  assert(server.begin(Server::NetworkMode::Hotspot));
  assert(devmode::depth == 2 && !server.clientSeen());
  assert(WiFi.apPassword.empty());  // Deliberately open game hotspot.
  assert(std::string(server.url()) == "http://192.168.4.1/battleship");  // QR avoids mDNS dependency.
  auto& ws = *WebSocketsServer::instance;
  auto& http = *WebServer::instance;
  http.routes.at("/battleship/session")();
  const auto token = http.response;
  assert(token.size() == 32);
  http.args["token"] = token;
  http.routes.at("/player/state")();
  assert(http.response.find("FORBIDDEN") != std::string::npos);  // Public bootstrap token cannot read profiles.
  const auto profile = "[\"profile\",\"" + token + "\",0,0,13,0]";
  ws.event(WStype_CONNECTED, 0);
  assert(server.clientSeen() && ws.messages.size() == 2);
  assert(ws.messages[0].find("\"type\":\"s\"") != std::string::npos);
  assert(ws.messages[1].find("\"type\":\"f\"") != std::string::npos);
  // Connecting to a public page must not disclose the active device Player.
  assert(ws.messages.back().find("SPIKY WINK BEARD") == std::string::npos);
  BrowserSnapshot state{BrowserPhase::Playing, true, true};
  server.publish(state);
  const auto count = ws.messages.size();
  assert(count == 4 && ws.messages[count - 2].find("playing") != std::string::npos);
  assert(ws.messages.back().find("\"type\":\"f\"") != std::string::npos);
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
  assert(ws.messages.size() >= count + 3);
  assert(ws.messages[ws.messages.size() - 3].find("placement") != std::string::npos);
  assert(ws.messages[ws.messages.size() - 2] == "{\"type\":\"peer\",\"name\":\"SPIKY WINK BEARD\"}");
  const std::string resumeMessage = ws.messages.back();
  const std::string prefix = "{\"type\":\"resume\",\"token\":\"";
  assert(resumeMessage.rfind(prefix, 0) == 0 && resumeMessage.size() == prefix.size() + 34);
  const std::string resumeToken = resumeMessage.substr(prefix.size(), 32);
  http.args["owner"] = "00000000000000000000000000000000";
  http.args["action"] = "guest";
  http.routes.at("/player/action")();
  assert(http.response.find("FORBIDDEN") != std::string::npos);
  http.args["owner"] = resumeToken;
  http.routes.at("/player/state")();
  assert(http.response.find("true") != std::string::npos);
  http.routes.at("/player/action")();
  assert(http.response.find("PLAYER LOCKED DURING MATCH") != std::string::npos);

  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 1);
  clockMs += 100;
  ws.event(WStype_TEXT, 0, profile);
  assert(handler.calls == 2);
  ws.event(WStype_CONNECTED, 1);
  assert(ws.messages[ws.messages.size() - 2].find("playing") != std::string::npos);
  assert(ws.messages.back().find("\"type\":\"f\"") != std::string::npos);
  assert(ws.messages.back().find("SPIKY WINK BEARD") == std::string::npos);
  assert(ws.recipients.back() == 1);
  // Profile identity changes must be delivered to the claimed seat, never
  // broadcast to a second connected (unclaimed) browser.
  server.publish(BrowserSnapshot{BrowserPhase::Placement, true, false});
  const auto beforeProfileAction = ws.messages.size();
  http.routes.at("/player/action")();
  assert(http.response.find("true") != std::string::npos);
  assert(ws.messages.size() == beforeProfileAction + 1);
  assert(ws.recipients.back() == 0);
  assert(ws.messages.back().find("\"type\":\"peer\"") != std::string::npos);
  server.publish(state);
  ws.event(WStype_TEXT, 1, profile);
  assert(handler.calls == 2 && ws.messages.back().find("error") != std::string::npos);
  ws.event(WStype_DISCONNECTED, 1);
  assert(server.clientSeen() && handler.disconnects == 0);
  ws.event(WStype_DISCONNECTED, 0);
  assert(!server.clientSeen() && handler.disconnects == 1);
  http.routes.at("/player/state")();
  assert(http.response.find("FORBIDDEN") != std::string::npos);  // Seat revoked on disconnect.

  http.routes.at("/battleship/session")();
  const auto freshToken = http.response;
  assert(freshToken != token);
  ws.event(WStype_CONNECTED, 0);
  assert(ws.messages.back().find("\"type\":\"f\"") != std::string::npos);
  const auto resume = "[\"resume\",\"" + freshToken + "\",2,\"" + resumeToken + "\"]";
  clockMs += 100;
  ws.event(WStype_TEXT, 0, resume);
  assert(handler.calls == 3);
  assert(ws.messages[ws.messages.size() - 5].find("placement") != std::string::npos);
  assert(ws.messages[ws.messages.size() - 4].find("\"type\":\"peer\"") != std::string::npos);
  assert(ws.messages[ws.messages.size() - 3].find("resume") != std::string::npos);
  assert(ws.messages[ws.messages.size() - 2].find("playing") != std::string::npos);
  assert(ws.messages.back().find("\"type\":\"f\"") != std::string::npos);
  http.args["token"] = freshToken;
  http.routes.at("/player/state")();
  assert(http.response.find("true") != std::string::npos);  // Same owner restored.

  // The previous browser connection can still be alive when a tab reloads.
  ws.event(WStype_CONNECTED, 1);
  clockMs += 100;
  ws.event(WStype_TEXT, 1, "[\\\"resume\\\",\\\""+freshToken+"\\\",2,\\\""+resumeToken+"\\\"]");
  assert(handler.calls == 4);
  assert(ws.recipients[ws.recipients.size()-4] == 1);
  ws.event(WStype_DISCONNECTED, 0);
  assert(server.clientSeen());
  http.routes.at("/player/state")();
  assert(http.response.find("true") != std::string::npos);
  ws.event(WStype_DISCONNECTED, 1);
  http.routes.at("/battleship/session")();
  const auto nextToken = http.response;
  ws.event(WStype_CONNECTED, 0);
  clockMs += 100;
  ws.event(WStype_TEXT, 0, "[\"resume\",\"" + nextToken + "\",2,\"00000000000000000000000000000000\"]");
  assert(handler.calls == 4 && ws.messages.back().find("error") != std::string::npos);

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
  puts("Browser server: ownership, battle/fleet/peer frames, session/resume capabilities, reconnect, AP/LAN and nested radio yield passed");
}
