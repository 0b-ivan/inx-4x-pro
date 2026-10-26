#include "platform/sim_biscuit_clock.h"
#include <cstdio>
#include <cstdlib>

static void check(bool okay, const char* msg) {
  if (!okay) { std::fprintf(stderr, "FAIL biscuit-sim: %s\n", msg); std::exit(1); }
}
int main() {
  std::tm result{};
  check(!biscuit_sim::readClockUtc(nullptr), "null destination rejected");
  check(setenv("CROSSPLAY_SIM_EPOCH", "1704067200", 1) == 0, "set fixture");
  check(biscuit_sim::readClockUtc(&result), "valid timestamp accepted");
  check(result.tm_year == 124 && result.tm_mon == 0 && result.tm_mday == 1 &&
        result.tm_hour == 0 && result.tm_min == 0, "2024-01-01 UTC exact");
  check(setenv("CROSSPLAY_SIM_EPOCH", "1704067261", 1) == 0, "set second fixture");
  check(biscuit_sim::readClockUtc(&result), "next timestamp accepted");
  check(result.tm_hour == 0 && result.tm_min == 1 && result.tm_sec == 1, "elapsed fixture");
  check(setenv("CROSSPLAY_SIM_EPOCH", "not-a-timestamp", 1) == 0, "invalid fixture setup");
  check(!biscuit_sim::readClockUtc(&result), "malformed fixture rejected");
  check(setenv("CROSSPLAY_SIM_EPOCH", "-123", 1) == 0, "negative setup");
  check(!biscuit_sim::readClockUtc(&result), "negative timestamp rejected");
  check(unsetenv("CROSSPLAY_SIM_EPOCH") == 0, "unset fixture");
  check(biscuit_sim::readClockUtc(&result), "host time available");
  std::puts("biscuit-sim clock: 13 checks, 0 failed");
}
