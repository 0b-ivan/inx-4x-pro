#include "platform/sim_biscuit_network.h"
#include <cstdio>
#include <cstdlib>
static int checks = 0;
static void check(bool b, const char* what) {
  ++checks;
  if (!b) { std::fprintf(stderr, "FAIL biscuit-sim network: %s\n", what); std::exit(1); }
}
int main() {
  uint8_t ip[4] = {};
  check(biscuit_sim::resolveHost("example.test", ip), "fixture resolves");
  check(ip[0] == 192 && ip[1] == 0 && ip[2] == 2 && ip[3] == 10, "fixture address");
  check(biscuit_sim::resolveHost("gateway.test", ip), "gateway");
  check(ip[3] == 1, "gateway address");
  check(biscuit_sim::resolveHost("localhost", ip), "localhost");
  check(ip[0] == 127 && ip[3] == 1, "loopback");
  check(!biscuit_sim::resolveHost("unknown.test", ip), "unknown lookup fails");
  check(!biscuit_sim::resolveHost("", ip), "empty lookup fails");
  unsetenv("CROSSPLAY_SIM_WIFI_CONNECTED");
  check(!biscuit_sim::wifiConnected(false), "disconnected");
  check(biscuit_sim::wifiConnected(true), "connected");
  setenv("CROSSPLAY_SIM_WIFI_CONNECTED", "0", 1);
  check(!biscuit_sim::wifiConnected(true), "force offline");
  setenv("CROSSPLAY_SIM_WIFI_CONNECTED", "1", 1);
  check(biscuit_sim::wifiConnected(false), "force online");
  unsetenv("CROSSPLAY_SIM_WIFI_CONNECTED");
  check(biscuit_sim::tcpProbe("example.test", 80), "fixture TCP reachable");
  check(biscuit_sim::tcpProbe("gateway.test", 80), "gateway TCP reachable");
  check(biscuit_sim::tcpProbe("localhost", 80), "loopback TCP reachable");
  check(!biscuit_sim::tcpProbe("gateway.test", 22), "no additional open ports");
  check(!biscuit_sim::tcpProbe("unknown.test", 80), "unknown host unreachable");
  check(!biscuit_sim::tcpProbe("8.8.8.8", 80), "no real internet probe");
  std::printf("biscuit-sim network: %d checks, 0 failed\n", checks);
}
