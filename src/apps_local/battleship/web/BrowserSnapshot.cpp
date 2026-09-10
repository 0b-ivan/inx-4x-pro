#include "BrowserSnapshot.h"

#include <cstdio>
#include <cstring>

#include "../BattleshipCore.h"

namespace bshipweb {
namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t encode64(const uint8_t* bytes, const size_t size, char* out) {
  size_t at = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < size; ++i) {
    acc = (acc << 8) | bytes[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out[at++] = kB64[(acc >> bits) & 63u];
    }
  }
  if (bits) out[at++] = kB64[(acc << (6 - bits)) & 63u];
  out[at] = '\0';
  return at;
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

  // Two bits per X4 cell: bit0=shot, bit1=hit. 100 cells -> 25 bytes -> 34 chars.
  uint8_t board[25] = {};
  for (int cell = 0; cell < bship::kCells; ++cell) {
    const bool shot = (snapshot.shotsAtX4[cell / 8] & (1u << (cell % 8))) != 0;
    const bool hit = (snapshot.hitsAtX4[cell / 8] & (1u << (cell % 8))) != 0;
    const uint8_t value = static_cast<uint8_t>((shot ? 1u : 0u) | (hit ? 2u : 0u));
    const int bit = cell * 2;
    board[bit / 8] |= static_cast<uint8_t>(value << (bit % 8));
  }

  char board64[35];
  char incoming64[19];
  encode64(board, sizeof(board), board64);
  encode64(snapshot.shotsAtBrowser, sizeof(snapshot.shotsAtBrowser), incoming64);

  const int count = snprintf(out, capacity,
                             "{\"type\":\"state\",\"phase\":\"%s\",\"myTurn\":%s,\"winner\":%d,\"b\":\"%s\",\"i\":\"%s\"}",
                             phase, snapshot.myTurn ? "true" : "false", snapshot.winner, board64, incoming64);
  if (count < 0 || static_cast<size_t>(count) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(count);
}
}  // namespace bshipweb
