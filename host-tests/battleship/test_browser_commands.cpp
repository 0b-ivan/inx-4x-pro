#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/apps_local/battleship/web/BrowserPlayer.h"
using namespace bshipweb;
static bool parse(const std::string& text, Command& c) {
  return parseCommand(reinterpret_cast<const uint8_t*>(text.data()), text.size(), c);
}
int main() {
  const std::string token = "\"0123456789abcdef0123456789abcdef\"";
  Command c;
  assert(parse("[\"profile\"," + token + ",0,0,13,0]", c));
  assert(c.value[1] == 13);
  for (const auto& bad : {"-1", "14", "256", "1.0", "1e0", "true", "null", "\"1\"", "01", "42949672960", "[]", "{}"}) {
    assert(!parse("[\"profile\"," + token + ",0,0," + bad + ",0]", c));
  }
  for (const auto& bad : {"[\"fire\",", "[\"name\",", "[\"avatar\","})
    assert(!parse(std::string(bad) + token + ",0,0,0,0]", c));
  assert(!parse("[\"profile\"," + token + ",0,0,0]", c));
  assert(!parse("[\"profile\"," + token + ",0,0,0,0,0]", c));
  assert(!parse("[\"ready\"," + token + ",0]null", c));
  assert(!parse("[\"ready\"," + token + ",-1]", c));
  assert(!parse("[\"ready\"," + token + ",4294967296]", c));
  assert(!parse(std::string(129, ' '), c));
  assert(!parseCommand(nullptr, 5, c));
  assert(parse(" \n[\"ready\", " + token + ", 4294967295]\t", c));
  std::string nul = "[\"ready\"," + token + ",0]";
  nul.push_back(0);
  assert(!parse(nul, c));
  assert(!parse("[\"ready\",\"0123456789abcdef0123456789abcdeg\",0]", c));

  bship::Game game;
  game.turn = 1;
  BrowserPlayer player;
  Command action;
  action.kind = CommandKind::Place;
  assert(!player.apply(game, action));
  action.kind = CommandKind::Profile;
  for (int a = 0; a < 14; ++a)
    for (int b = 0; b < 14; ++b)
      for (int d = 0; d < 14; ++d) {
        player.reset();
        action.revision = 0;
        action.value[0] = a;
        action.value[1] = b;
        action.value[2] = d;
        assert(player.apply(game, action));
        const auto parts = player::parse(player.name());
        assert(parts.known() && parts.word[0] == a && parts.word[1] == b && parts.word[2] == d);
      }
  auto profile = player.view();
  action.revision = profile.revision;
  action.value[0] = 255;
  assert(!player.apply(game, action));
  assert(player.view().revision == profile.revision);
  action.kind = CommandKind::Ready;
  assert(!player.apply(game, action));
  assert(!game.side[1].placed);
  action.kind = CommandKind::Place;
  for (int i = 0; i < 5; ++i) {
    action.value[0] = i;
    action.value[1] = i * 10;
    action.value[2] = 1;
    action.revision = player.view().revision;
    assert(player.apply(game, action));
    assert(!player.apply(game, action));  // duplicate/replayed revision
  }
  action.revision = player.view().revision;
  action.value[0] = 0;
  action.value[1] = 9;
  action.value[2] = 1;
  assert(!player.apply(game, action));  // row wrap
  action.value[1] = 90;
  action.value[2] = 0;
  assert(!player.apply(game, action));  // bottom edge
  action.value[1] = 10;
  action.value[2] = 1;
  assert(!player.apply(game, action));  // overlaps second ship
  action.value[0] = 5;
  assert(!player.apply(game, action));
  action.value[0] = 0;
  action.value[2] = 2;
  assert(!player.apply(game, action));
  game.turn = 0;
  action.kind = CommandKind::Ready;
  assert(!player.apply(game, action));
  game.turn = 1;
  const auto hostBefore = game.side[0];
  assert(player.apply(game, action));
  assert(game.side[1].placed && game.turn == 0 && player.view().ready);
  assert(!memcmp(&hostBefore, &game.side[0], sizeof(hostBefore)));
  action.revision = player.view().revision;
  assert(!player.apply(game, action));
  action.kind = CommandKind::Profile;
  assert(!player.apply(game, action));
  char expected[256], actual[256];
  assert(serializePlacement(player.view(), true, expected, sizeof(expected)));
  uint32_t seed = 123;
  for (int i = 0; i < 1000; ++i) {
    bship::randomFleet(game.side[0].fleet, seed);
    serializePlacement(player.view(), true, actual, sizeof(actual));
    assert(!strcmp(expected, actual));
  }
  assert(!serializePlacement(player.view(), true, actual, 5) && actual[0] == 0);
  player.reset();
  assert(!player.view().profile && player.view().bow[0] == 255 && player.name()[0] == 0);
  action.revision = 0;
  assert(!player.apply(game, action));  // a committed seat cannot be reclaimed
  puts("Browser commands: strict parsing, 2744 profiles, placement, replay, ready and private projection passed");
}
