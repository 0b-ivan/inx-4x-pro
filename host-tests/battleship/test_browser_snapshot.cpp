#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

#include "../../src/apps_local/battleship/BattleshipCore.h"
#include "../../src/apps_local/battleship/web/BrowserSnapshot.h"
using namespace bshipweb;
static_assert(!std::is_invocable_v<decltype(serializeSnapshot), const bship::Game&, char*, size_t>);
static_assert(sizeof(BrowserSnapshot) <= 64);

int main() {
  bship::Game game;
  bship::reset(game);
  assert(browserSnapshot(game, false).phase == BrowserPhase::Waiting);
  assert(browserSnapshot(game, true).phase == BrowserPhase::Placement);
  assert(!browserSnapshot(game, true).myTurn);

  uint32_t seed = 123;
  bship::randomFleet(game.side[0].fleet, seed);
  bship::randomFleet(game.side[1].fleet, seed);
  game.side[1].placed = 1;
  assert(browserSnapshot(game, true).phase == BrowserPhase::Waiting);
  game.side[0].placed = 1;
  assert(browserSnapshot(game, true).phase == BrowserPhase::Playing);
  game.turn = 1;
  assert(browserSnapshot(game, true).myTurn);

  char expected[128], actual[128];
  const size_t expectedSize = serializeSnapshot(browserSnapshot(game, true), expected, sizeof(expected));
  assert(expectedSize > 0 && expectedSize < sizeof(expected));
  assert(strstr(expected, "\"type\":\"state\"") != nullptr);
  assert(strstr(expected, "\"phase\":\"playing\"") != nullptr);
  assert(strstr(expected, "\"myTurn\":true") != nullptr);
  assert(strstr(expected, "\"b\":\"") != nullptr);
  assert(strstr(expected, "\"i\":\"") != nullptr);

  // With no shots, changing the hidden X4 fleet cannot change the browser payload.
  for (int i = 0; i < 1000; ++i) {
    bship::randomFleet(game.side[0].fleet, seed);
    serializeSnapshot(browserSnapshot(game, true), actual, sizeof(actual));
    assert(strcmp(actual, expected) == 0);
  }

  // Once a cell is fired at, only the public shot/hit result may affect the projection.
  game.turn = 1;
  assert(bship::fire(game, 0));
  const auto afterShot = browserSnapshot(game, true);
  assert(afterShot.shotsAtX4[0] & 1u);
  assert(!afterShot.myTurn);
  assert(serializeSnapshot(afterShot, actual, sizeof(actual)) > 0);
  assert(strcmp(actual, expected) != 0);

  for (int cell = 0; cell < bship::kCells; ++cell) bship::markShot(game.side[0], cell);
  assert(browserSnapshot(game, true).phase == BrowserPhase::Finished);
  assert(!browserSnapshot(game, true).myTurn);
  assert(!sameSnapshot(browserSnapshot(game, true), browserSnapshot(game, false)));
  assert(sameSnapshot(browserSnapshot(game, true), browserSnapshot(game, true)));
  assert(serializeSnapshot({}, nullptr, 0) == 0);
  actual[0] = 'x';
  assert(serializeSnapshot({}, actual, 2) == 0 && actual[0] == 0);
  puts("Browser snapshot: phases, turns, battle projection, secrecy and capacity passed");
}
