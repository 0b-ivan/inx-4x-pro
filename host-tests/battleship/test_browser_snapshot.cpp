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

  char expected[128], actual[128], fleetExpected[64], fleetActual[64];
  const size_t expectedSize = serializeSnapshot(browserSnapshot(game, true), expected, sizeof(expected));
  const size_t fleetExpectedSize = serializeFleetStatus(browserSnapshot(game, true), fleetExpected, sizeof(fleetExpected));
  assert(expectedSize > 0 && expectedSize < sizeof(expected));
  assert(fleetExpectedSize > 0 && fleetExpectedSize < sizeof(fleetExpected));
  assert(strstr(expected, "\"type\":\"s\"") != nullptr);
  assert(strstr(expected, "\"phase\":\"playing\"") != nullptr);
  assert(strstr(expected, "\"myTurn\":true") != nullptr);
  assert(strstr(expected, "\"b\":\"") != nullptr);
  assert(strstr(expected, "\"i\":\"") != nullptr);
  assert(strcmp(fleetExpected, "{\"type\":\"f\",\"h\":[0,0,0,0,0]}") == 0);

  // With no shots, changing the hidden X4 fleet cannot change either public payload.
  for (int i = 0; i < 1000; ++i) {
    bship::randomFleet(game.side[0].fleet, seed);
    const auto projection = browserSnapshot(game, true);
    serializeSnapshot(projection, actual, sizeof(actual));
    serializeFleetStatus(projection, fleetActual, sizeof(fleetActual));
    assert(strcmp(actual, expected) == 0);
    assert(strcmp(fleetActual, fleetExpected) == 0);
  }

  // A hit may reveal its result and the damaged ship's hit count, but never that
  // ship's hidden bow/orientation/cells.
  const int target = bship::shipCell(game.side[0].fleet.ships[0], 0);
  game.turn = 1;
  assert(bship::fire(game, target));
  const auto afterShot = browserSnapshot(game, true);
  assert(afterShot.shotsAtX4[target / 8] & (1u << (target % 8)));
  assert(afterShot.hitsByX4Ship[0] == 1);
  assert(!afterShot.myTurn);
  assert(serializeSnapshot(afterShot, actual, sizeof(actual)) > 0);
  assert(serializeFleetStatus(afterShot, fleetActual, sizeof(fleetActual)) > 0);
  assert(strstr(fleetActual, "\"h\":[1,0,0,0,0]") != nullptr);
  assert(strcmp(actual, expected) != 0);

  for (int cell = 0; cell < bship::kCells; ++cell) bship::markShot(game.side[0], cell);
  const auto finished = browserSnapshot(game, true);
  assert(finished.phase == BrowserPhase::Finished);
  assert(!finished.myTurn);
  assert(serializeSnapshot(finished, actual, sizeof(actual)) > 0);
  assert(serializeFleetStatus(finished, fleetActual, sizeof(fleetActual)) > 0);
  assert(strlen(actual) < sizeof(actual));
  assert(strlen(fleetActual) < sizeof(fleetActual));
  assert(!sameSnapshot(browserSnapshot(game, true), browserSnapshot(game, false)));
  assert(sameSnapshot(browserSnapshot(game, true), browserSnapshot(game, true)));
  assert(serializeSnapshot({}, nullptr, 0) == 0);
  assert(serializeFleetStatus({}, nullptr, 0) == 0);
  actual[0] = 'x';
  fleetActual[0] = 'x';
  assert(serializeSnapshot({}, actual, 2) == 0 && actual[0] == 0);
  assert(serializeFleetStatus({}, fleetActual, 2) == 0 && fleetActual[0] == 0);
  puts("Browser snapshot: phases, turns, battle projection, public ship damage, secrecy and capacity passed");
}
