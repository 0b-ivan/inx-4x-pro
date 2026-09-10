#include "BrowserSnapshot.h"

#include <cstdio>
#include <cstring>

#include "../BattleshipCore.h"

namespace bshipweb {
namespace {
char hexDigit(const uint8_t value) { return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10); }

void encodeMask(const uint8_t* bytes, char* out) {
  for (int i = 0; i < 13; ++i) {
    out[i * 2] = hexDigit(static_cast<uint8_t>(bytes[i] >> 4));
    out[i * 2 + 1] = hexDigit(static_cast<uint8_t>(bytes[i] & 0x0f));
  }
  out[26] = '\0';
}
}  // namespace

BrowserSnapshot browserSnapshot(const bship::Game& game, const bool connected) {
  BrowserSnapshot result;
  result.connected = connected;
  if (!connected) return result;

  constexpr int browserSide = 1;
  constexpr int x4Side = 0;
  if (bship::over(game))
    result.phase = BrowserPhase::Finished;
  else if (bship::bothPlaced(game))
    result.phase = BrowserPhase::Playing;
  else if (!game.side[browserSide].placed)
    result.phase = BrowserPhase::Placement;
  else
    result.phase = BrowserPhase::Waiting;

  result.myTurn = result.phase == BrowserPhase::Playing && game.turn == browserSide;
  result.winner = static_cast<int8_t>(bship::winner(game));

  for (int i = 0; i < 13; ++i) {
    result.shotsAtX4[i] = game.side[x4Side].shots[i];
    result.shotsAtBrowser[i] = game.side[browserSide].shots[i];
  }

  // Reveal hit information only for cells that have actually been fired at.
  for (int cell = 0; cell < bship::kCells; ++cell) {
    if (!bship::shotAt(game.side[x4Side], cell)) continue;
    if (bship::shipAt(game.side[x4Side].fleet, cell) < 0) continue;
    result.hitsAtX4[cell / 8] |= static_cast<uint8_t>(1u << (cell % 8));
  }
  return result;
}

bool sameSnapshot(const BrowserSnapshot& a, const BrowserSnapshot& b) {
  return a.phase == b.phase && a.connected == b.connected && a.myTurn == b.myTurn && a.winner == b.winner &&
         memcmp(a.shotsAtX4, b.shotsAtX4, sizeof(a.shotsAtX4)) == 0 &&
         memcmp(a.hitsAtX4, b.hitsAtX4, sizeof(a.hitsAtX4)) == 0 &&
         memcmp(a.shotsAtBrowser, b.shotsAtBrowser, sizeof(a.shotsAtBrowser)) == 0;
}

size_t serializeSnapshot(const BrowserSnapshot& snapshot, char* out, const size_t capacity) {
  if (!out || !capacity) return 0;
  const char* phase = "waiting";
  switch (snapshot.phase) {
    case BrowserPhase::Placement:
      phase = "placement";
      break;
    case BrowserPhase::Playing:
      phase = "playing";
      break;
    case BrowserPhase::Finished:
      phase = "finished";
      break;
    case BrowserPhase::Waiting:
      break;
  }

  char shots[27];
  char hits[27];
  char incoming[27];
  encodeMask(snapshot.shotsAtX4, shots);
  encodeMask(snapshot.hitsAtX4, hits);
  encodeMask(snapshot.shotsAtBrowser, incoming);

  const int count = snprintf(
      out, capacity,
      "{\"type\":\"state\",\"phase\":\"%s\",\"connected\":%s,\"myTurn\":%s,\"winner\":%d,"
      "\"shots\":\"%s\",\"hits\":\"%s\",\"incoming\":\"%s\"}",
      phase, snapshot.connected ? "true" : "false", snapshot.myTurn ? "true" : "false", snapshot.winner, shots, hits,
      incoming);
  if (count < 0 || static_cast<size_t>(count) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(count);
}
}  // namespace bshipweb
