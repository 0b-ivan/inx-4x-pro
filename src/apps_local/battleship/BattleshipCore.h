#pragma once

// Battleship rules and a gunner, freestanding.
//
// No Arduino, no renderer, no heap, no allocation of any kind: the whole game
// is 52 bytes of plain struct, so host-tests/battleship/ runs the rules on a
// laptop in milliseconds and host-tests/link/ can play whole games between two
// simulated devices. Only BattleshipActivity draws.
//
// ---------------------------------------------------------------------------
// The state is the wire format, deliberately.
//
// Chess sends a FEN because its position has a text form older than computers.
// Battleship has no such thing, so `Game` itself is what travels: it is
// trivially copyable, it is 52 bytes against the link's 192-byte packet, and
// making it the wire format means there is exactly one description of a game in
// this app instead of two that can drift.
//
// Everything that can be derived is derived rather than stored -- who has won,
// whether a ship is sunk, whether the fleets are placed. A stored winner is a
// field two devices can disagree about; a computed one cannot be.
//
// `Game` holds BOTH fleets, including the one its owner must not see. That is
// the only shape that keeps whole-state-not-moves (and with it, desync being
// structurally impossible), and it is safe for the same reason the rest of this
// works: a match is two X4 Pros in the same room running the same build. The
// secret is kept by the drawing code, which never asks the opposing fleet where
// it is until the game is over. See BattleshipActivity::drawTargetGrid.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace bship {

constexpr int kSize = 10;
constexpr int kCells = kSize * kSize;
constexpr int kShipCount = 5;
// Milton Bradley's fleet: carrier, battleship, cruiser, submarine, destroyer.
constexpr uint8_t kShipLength[kShipCount] = {5, 4, 3, 3, 2};
constexpr int kFleetCells = 17;
// One bit per cell, and 100 bits do not divide by 8.
constexpr int kShotBytes = (kCells + 7) / 8;

constexpr int cellOf(const int row, const int col) { return row * kSize + col; }
constexpr int rowOf(const int cell) { return cell / kSize; }
constexpr int colOf(const int cell) { return cell % kSize; }

struct Ship {
  uint8_t bow = 0;
  uint8_t horizontal = 1;
};

struct Fleet {
  Ship ships[kShipCount] = {};
};

struct Side {
  Fleet fleet;
  uint8_t shots[kShotBytes] = {};
  uint8_t placed = 0;
};

struct Game {
  Side side[2] = {};
  uint8_t turn = 0;
  uint8_t lastShot = 0;
};

const char* shipName(int index);
void cellName(int cell, char* out);
uint32_t nextRandom(uint32_t& seed);

int shipCell(const Ship& ship, int index);
bool canPlace(const Fleet& fleet, int shipIndex, const Ship& candidate);
int shipAt(const Fleet& fleet, int cell);
void randomFleet(Fleet& fleet, uint32_t& seed);

bool shotAt(const Side& side, int cell);
void markShot(Side& side, int cell);
bool sunk(const Side& side, int shipIndex);
int sunkCount(const Side& side);
bool defeated(const Side& side);
int shotsTaken(const Side& side);
int hitsTaken(const Side& side);

void reset(Game& game);
bool bothPlaced(const Game& game);
bool over(const Game& game);
int winner(const Game& game);
bool place(Game& game, int side, const Fleet& fleet);
bool fire(Game& game, int cell);

// Surrender is a terminal rules action, not a UI convention. The side that
// surrenders is defeated and the other side is the winner. Keeping that rule in
// the core prevents a browser/device path from accidentally awarding a win to
// the player who pressed SURRENDER.
inline bool surrender(Game& game, const int side) {
  if (side < 0 || side > 1 || !bothPlaced(game) || over(game)) return false;
  for (int ship = 0; ship < kShipCount; ++ship) {
    for (int segment = 0; segment < kShipLength[ship]; ++segment) {
      markShot(game.side[side], shipCell(game.side[side].fleet.ships[ship], segment));
    }
  }
  game.lastShot = 0;
  return over(game) && winner(game) == (side ^ 1);
}

bool lastShotHit(const Game& game);
int lastShotSank(const Game& game);
int chooseShot(const Side& target, uint32_t& seed);

}  // namespace bship
