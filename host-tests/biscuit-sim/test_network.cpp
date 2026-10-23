#include "platform/sim_biscuit_network.h"
#include "platform/sim_biscuit_ble.h"
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
  check(!biscuit_sim::resolveHost("example.com", ip), "public DNS must not resolve");
  check(!biscuit_sim::resolveHost("EXAMPLE.TEST", ip), "fixture names are exact");
  check(!biscuit_sim::resolveHost("example.test.evil", ip), "fixture suffix bypass blocked");
  check(!biscuit_sim::resolveHost("example.test.", ip), "trailing dot is not silently normalized");
  unsetenv("CROSSPLAY_SIM_WIFI_CONNECTED");
  check(!biscuit_sim::wifiConnected(false), "disconnected");
  check(biscuit_sim::wifiConnected(true), "connected");
  setenv("CROSSPLAY_SIM_WIFI_CONNECTED", "0", 1);
  check(!biscuit_sim::wifiConnected(true), "force offline");
  setenv("CROSSPLAY_SIM_WIFI_CONNECTED", "1", 1);
  check(biscuit_sim::wifiConnected(false), "force online");
  setenv("CROSSPLAY_SIM_WIFI_CONNECTED", "invalid", 1);
  check(biscuit_sim::wifiConnected(true), "invalid override preserves connected status");
  check(!biscuit_sim::wifiConnected(false), "invalid override preserves disconnected status");
  unsetenv("CROSSPLAY_SIM_WIFI_CONNECTED");
  check(biscuit_sim::tcpProbe("example.test", 80), "fixture TCP reachable");
  check(biscuit_sim::tcpProbe("gateway.test", 80), "gateway TCP reachable");
  check(biscuit_sim::tcpProbe("localhost", 80), "loopback TCP reachable");
  check(!biscuit_sim::tcpProbe("gateway.test", 22), "no additional open ports");
  check(!biscuit_sim::tcpProbe("unknown.test", 80), "unknown host unreachable");
  check(!biscuit_sim::tcpProbe("8.8.8.8", 80), "no real internet probe");
  check(!biscuit_sim::tcpProbe("example.com", 80), "public hostname cannot be probed");
  check(!biscuit_sim::tcpProbe("example.test", 443), "HTTPS port not open by default");
  check(!biscuit_sim::tcpProbe("example.test", 0), "invalid port unavailable");
  check(!biscuit_sim::tcpProbe("EXAMPLE.TEST", 80), "TCP fixture case does not widen access");
  check(biscuit_sim::hostScanIp(1) == 0x010200C0U, "fixture first host IP");
  check(biscuit_sim::hostScanIp(20) == 0x140200C0U, "fixture second host IP");
  check(biscuit_sim::hostScanIp(0) == 0, "network address excluded");
  check(biscuit_sim::hostScanIp(31) == 0, "broadcast address excluded");
  check(biscuit_sim::hostScanPortOpen(biscuit_sim::hostScanIp(1), 80), "first fixture host reachable");
  check(biscuit_sim::hostScanPortOpen(biscuit_sim::hostScanIp(20), 80), "second fixture host reachable");
  check(!biscuit_sim::hostScanPortOpen(biscuit_sim::hostScanIp(2), 80), "unknown fixture host unreachable");
  check(!biscuit_sim::hostScanPortOpen(biscuit_sim::hostScanIp(1), 22), "SSH port closed");
  check(!biscuit_sim::hostScanPortOpen(0x08080808U, 80), "public IPv4 never reachable");
  auto response = biscuit_sim::httpRequest("GET", "http://example.test/ok", "");
  check(response.status == 200 && response.body == "simulated OK", "HTTP 200 fixture");
  response = biscuit_sim::httpRequest("GET", "http://example.test/missing", "");
  check(response.status == 404, "HTTP 404 fixture");
  response = biscuit_sim::httpRequest("POST", "http://example.test/echo", "fixture-body");
  check(response.status == 200 && response.body == "fixture-body", "HTTP POST echo");
  response = biscuit_sim::httpRequest("GET", "http://example.test/timeout", "");
  check(response.status < 0 && response.latencyMs == 5000, "HTTP timeout fixture");
  response = biscuit_sim::httpRequest("GET", "https://example.com/", "");
  check(response.status < 0, "no public HTTP connection");
  response = biscuit_sim::httpRequest("GET", "http://example.test.evil/ok", "");
  check(response.status < 0, "HTTP fixture hostname must match exactly");
  auto mdns = biscuit_sim::mdnsDiscover("_http._tcp");
  check(mdns.size() == 2, "mDNS HTTP service fixtures");
  check(mdns[0].port == 80 && std::string(mdns[0].ip) == "192.0.2.1", "mDNS gateway fixture");
  check(biscuit_sim::mdnsDiscover("_ssh._tcp").size() == 1, "mDNS SSH fixture");
  check(biscuit_sim::mdnsDiscover("_missing._tcp").empty(), "mDNS unknown type empty");
  check(biscuit_sim::mdnsDiscover("http").empty(), "mDNS exact service match");
  unsetenv("CROSSPLAY_SIM_BLE_SCENARIO");
  auto advertisements = biscuit_sim::bleAdvertisements();
  check(advertisements.size() == 2, "BLE two deterministic advertisements");
  check(std::string(advertisements[0].mac) == "02:00:00:00:00:11", "BLE stable synthetic MAC");
  check(advertisements[0].rssi > advertisements[1].rssi, "BLE RSSI ranking");
  setenv("CROSSPLAY_SIM_BLE_SCENARIO", "empty", 1);
  check(biscuit_sim::bleAdvertisements().empty(), "BLE no-device scenario");
  setenv("CROSSPLAY_SIM_BLE_SCENARIO", "timeout", 1);
  check(biscuit_sim::bleAdvertisements().empty(), "BLE timeout scenario");
  setenv("CROSSPLAY_SIM_BLE_SCENARIO", "weak", 1);
  advertisements = biscuit_sim::bleAdvertisements();
  check(advertisements.size() == 2 && advertisements[0].rssi == -93, "BLE weak-signal scenario");
  unsetenv("CROSSPLAY_SIM_BLE_SCENARIO");
  auto services = biscuit_sim::bleGattServices("02:00:00:00:00:11");
  check(services.size() == 2, "BLE synthetic sensor exposes GATT services");
  auto chars = biscuit_sim::bleGattCharacteristics("02:00:00:00:00:11", services[0].uuid);
  check(chars.size() == 1 && chars[0].canRead, "BLE battery characteristic readable");
  check(std::string(chars[0].uuid).find("2a19") != std::string::npos, "BLE battery UUID");
  check(biscuit_sim::bleConnects("02:00:00:00:00:22"), "BLE beacon connects in fixture");
  check(!biscuit_sim::bleConnects("AA:BB:CC:DD:EE:FF"), "BLE unknown address cannot connect");
  check(biscuit_sim::bleGattServices("AA:BB:CC:DD:EE:FF").empty(), "BLE no external service enumeration");
  check(biscuit_sim::bleGattCharacteristics("02:00:00:00:00:11", "0000ffff").empty(), "BLE unknown service yields no values");
  setenv("CROSSPLAY_SIM_BLE_SCENARIO", "disconnect", 1);
  check(!biscuit_sim::bleConnects("02:00:00:00:00:11"), "BLE simulated connection failure");
  check(biscuit_sim::bleGattServices("02:00:00:00:00:11").empty(), "BLE connection failure hides services");
  unsetenv("CROSSPLAY_SIM_BLE_SCENARIO");
  std::printf("biscuit-sim network: %d checks, 0 failed\n", checks);
}
