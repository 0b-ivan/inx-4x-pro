#include <cassert>

#include "../../src/apps_local/battleship/BattleshipCore.h"

namespace {

bship::Fleet fleet(uint8_t offset) {
  bship::Fleet value{};
  value.ships[0] = {static_cast<uint8_t>(offset + 0), 1};
  value.ships[1] = {static_cast<uint8_t>(offset + 10), 1};
  value.ships[2] = {static_cast<uint8_t>(offset + 20), 1};
  value.ships[3] = {static_cast<uint8_t>(offset + 30), 1};
  value.ships[4] = {static_cast<uint8_t>(offset + 40), 1};
  return value;
}

bship::Game readyGame() {
  bship::Game game{};
  const bship::Fleet first = fleet(0);
  const bship::Fleet second = fleet(5);
  assert(bship::place(game, 0, first));
  assert(bship::place(game, 1, second));
  return game;
}

}  // namespace

int main() {
  {
    bship::Game game = readyGame();
    assert(bship::surrender(game, 0));
    assert(bship::over(game));
    assert(bship::winner(game) == 1);
    assert(bship::defeated(game.side[0]));
    assert(!bship::defeated(game.side[1]));
  }

  {
    bship::Game game = readyGame();
    assert(bship::surrender(game, 1));
    assert(bship::over(game));
    assert(bship::winner(game) == 0);
    assert(bship::defeated(game.side[1]));
    assert(!bship::defeated(game.side[0]));
  }

  {
    bship::Game game = readyGame();
    assert(bship::surrender(game, 0));
    assert(!bship::surrender(game, 0));
    assert(!bship::surrender(game, 1));
    assert(bship::winner(game) == 1);
  }

  return 0;
}
