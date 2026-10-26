#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../src/apps_local/web/SeatSession.h"

namespace {
uint32_t sequence = 0x13572468u;
uint32_t randomWord() { return sequence += 0x9e3779b9u; }
}
int main() {
  webplay::SeatSession seat;
  seat.begin(randomWord);
  char publicToken[33], secret[33];
  std::snprintf(publicToken, sizeof(publicToken), "%s", seat.token());
  std::snprintf(secret, sizeof(secret), "%s", seat.resumeToken());
  assert(std::strlen(publicToken) == 32 && std::strlen(secret) == 32);
  assert(!seat.hasOwner() && !seat.authorized(publicToken, secret));
  assert(!seat.admit(0, "bad", true, false, "", 100));
  assert(!seat.admit(0, publicToken, false, false, "", 100));
  assert(!seat.admit(0, publicToken, false, true, "bad", 100));
  assert(seat.admit(0, publicToken, true, false, "", 100));
  seat.claim(0, true, randomWord);
  assert(seat.hasOwner() && seat.owner() == 0);
  assert(!seat.authorized(publicToken, secret)); // New profile rotates seat secret.
  std::snprintf(secret, sizeof(secret), "%s", seat.resumeToken());
  assert(seat.authorized(publicToken, secret));
  assert(!seat.admit(1, publicToken, false, true, "00000000000000000000000000000000", 200));
  assert(!seat.admit(0, publicToken, true, false, "", 199));
  assert(seat.admit(0, publicToken, false, false, "", 200));
  assert(!seat.disconnect(1, randomWord));
  assert(seat.disconnect(0, randomWord));
  assert(!seat.hasOwner() && !seat.authorized(publicToken, secret));
  assert(std::strcmp(publicToken, seat.token()) != 0);
  std::snprintf(publicToken, sizeof(publicToken), "%s", seat.token());
  assert(!seat.admit(1, publicToken, false, true, "bad", 300));
  assert(seat.admit(1, publicToken, false, true, secret, 300));
  seat.claim(1, false, randomWord);
  assert(seat.owner() == 1 && seat.authorized(publicToken, secret));
  assert(!seat.admit(0, publicToken, true, false, "", 400));
  // Safari can open a new socket before the previous one disconnects.
  // A stranger cannot take over, but the same private browser can.
  assert(!seat.admit(2, publicToken, false, false, "", 500));
  assert(!seat.rebind(2, "00000000000000000000000000000000"));
  assert(seat.admit(2, publicToken, false, true, secret, 500));
  assert(seat.rebind(2, secret));
  assert(seat.owner() == 2 && seat.authorized(publicToken, secret));
  assert(!seat.disconnect(1, randomWord));
  assert(!seat.admit(1, publicToken, true, false, "", 700));
  seat.stop();
  assert(!seat.hasOwner() && !seat.authorized(publicToken, secret));
  assert(!seat.admit(1, publicToken, false, true, secret, 500));
  puts("Browser seat session: owner claim, 128-bit tokens, throttling, disconnect and authenticated resume passed");
}
