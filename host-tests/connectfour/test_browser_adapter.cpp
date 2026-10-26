#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../src/apps_local/connectfour/web/ConnectFourBrowserAdapter.h"

namespace {
uint32_t rng = 7;
uint32_t nextRandom() { return rng += 0x9e3779b9u; }
std::string make(const char* action, const std::string& token, uint32_t revision,
                 int col=-1, const std::string& secret="") {
  std::string msg = "[\"" + std::string(action) + "\",\"" + token + "\"," + std::to_string(revision);
  if (col >= 0) msg += "," + std::to_string(col);
  if (!secret.empty()) msg += ",\"" + secret + "\"";
  return msg + "]";
}
bool send(c4web::Round& r, uint8_t client, const std::string& msg, uint32_t time) {
  return r.receive(client, reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), time, nextRandom);
}
}
int main() {
  c4web::Round r; r.begin(nextRandom);
  const std::string token = r.token();
  char frame[192]{};
  assert(r.snapshot(frame, sizeof(frame)) > 0);
  assert(std::strstr(frame, "\"phase\":\"waiting\""));
  assert(std::strstr(frame, token.c_str()) == nullptr);
  assert(!r.deviceDrop(0));
  assert(!send(r, 0, make("drop", token, 0, 0), 100));
  assert(!send(r, 0, make("join", token, 1), 100));
  assert(!send(r, 0, make("join", token, 0)+"null", 100));
  assert(!send(r, 0, "[\"join\",\"0123456789abcdef0123456789abcdeg\",0]", 100));
  assert(!send(r, 0, make("drop", token, 0, 7), 100));
  assert(!send(r, 0, std::string(129, 'x'), 100));
  assert(send(r, 0, make("join", token, 0), 100));
  const std::string secret = r.resumeToken();
  assert(r.joined() && r.owner() == 0 && r.authorized(token.c_str(), secret.c_str()));
  assert(!send(r, 1, make("join", token, 0), 200));
  assert(!send(r, 1, make("drop", token, 0, 1), 200));
  assert(!send(r, 0, make("drop", token, 0, 1), 200)); // X4 opens
  for (uint32_t turn=0; turn<3; ++turn) {
    assert(r.deviceDrop(0));
    const auto rev=r.revision();
    assert(!send(r, 0, make("drop", token, rev-1, 1), 300+200*turn));
    assert(send(r, 0, make("drop", token, rev, 1), 300+200*turn));
    assert(!send(r, 0, make("drop", token, rev, 1), 400+200*turn));
  }
  assert(r.deviceDrop(0));
  assert(r.game().outcome == connectfour::Outcome::LightWins && r.revision() == 7);
  assert(!send(r, 0, make("drop", token, 7, 1), 1000));
  assert(r.snapshot(frame, sizeof(frame)) > 0);
  assert(std::strstr(frame, "\"phase\":\"finished\""));
  assert(std::strstr(frame, "\"outcome\":1"));
  assert(std::strstr(frame, secret.c_str()) == nullptr);
  assert(r.snapshot(frame, 18) == 0 && frame[0]=='\0');
  assert(!r.disconnected(1, nextRandom));
  assert(r.disconnected(0, nextRandom));
  assert(!r.hasOwner());
  const std::string fresh=r.token();
  assert(fresh != token);
  assert(!r.authorized(fresh.c_str(), secret.c_str()));
  assert(!send(r, 1, make("join", fresh, 7), 1100));
  assert(!send(r, 1, make("resume", fresh, 0, -1, std::string(32,'0')), 1100));
  assert(send(r, 1, make("resume", fresh, 0, -1, secret), 1100));
  assert(r.owner()==1 && r.authorized(fresh.c_str(), secret.c_str()));
  assert(!send(r, 0, make("rematch", fresh, 7), 1300));
  assert(send(r, 1, make("rematch", fresh, 7), 1300));
  assert(r.revision()==8 && r.game().outcome==connectfour::Outcome::Running);
  assert(connectfour::discCount(r.game())==0);
  assert(!send(r, 1, make("rematch", fresh, 8), 1450));
  assert(r.deviceDrop(4) && r.game().turn==connectfour::kDark);
  assert(send(r, 1, make("drop", fresh, 9, 6), 1600));
  assert(r.game().cell[6][0]==connectfour::kDark);
  r.stop();
  assert(!r.hasOwner() && !r.authorized(fresh.c_str(), secret.c_str()));
  puts("Connect Four browser: strict command parsing, exclusive seat, X4 turns, win, reconnect and rematch passed");
}
