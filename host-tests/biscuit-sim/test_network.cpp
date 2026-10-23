#include "platform/sim_biscuit_network.h"
#include "platform/sim_biscuit_ble.h"
#include "platform/sim_biscuit_wifi_frames.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <map>
#include <string>
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
  auto frames = biscuit_sim::bleContactPackets();
  check(frames.size() == 6, "BLE Contact synthetic and invalid fixtures");
  check(frames[0].size() == 29, "BLE contact manufacturer payload length");
  check(static_cast<unsigned char>(frames[0][0]) == 0xCC &&
        static_cast<unsigned char>(frames[0][1]) == 0xB1, "BLE contact company ID");
  check(frames[1][0] != frames[0][0], "BLE foreign company ID fixture");
  check(frames[0] == frames[3], "BLE duplicate fixture");
  check(frames[5].size() < 29, "BLE truncated frame fixture");
  setenv("CROSSPLAY_SIM_BLE_SCENARIO", "empty", 1);
  check(biscuit_sim::bleContactPackets().empty(), "BLE Contact empty scan");
  unsetenv("CROSSPLAY_SIM_BLE_SCENARIO");
  unsetenv("CROSSPLAY_SIM_TRACKER_SCENARIO");
  auto trackers = biscuit_sim::bleTrackerFrames();
  check(trackers.size() == 4, "tracker four synthetic advertisements");
  check(trackers[0].manufacturer.size() == 5 &&
        static_cast<uint8_t>(trackers[0].manufacturer[0]) == 0x4c &&
        static_cast<uint8_t>(trackers[0].manufacturer[2]) == 0x12,
        "tracker synthetic Apple frame");
  check(trackers[1].manufacturer.size() == 2 &&
        static_cast<uint8_t>(trackers[1].manufacturer[0]) == 0x75,
        "tracker synthetic Samsung frame");
  check(std::string(trackers[2].serviceUuid).find("feed") != std::string::npos,
        "tracker synthetic Tile service");
  check(!biscuit_sim::bleTrackerFollowerScenario(), "normal scan has no forced follower");
  setenv("CROSSPLAY_SIM_TRACKER_SCENARIO", "follower", 1);
  trackers = biscuit_sim::bleTrackerFrames();
  check(trackers.size() == 1 && biscuit_sim::bleTrackerFollowerScenario(),
        "virtual follower duration scenario");
  setenv("CROSSPLAY_SIM_TRACKER_SCENARIO", "empty", 1);
  check(biscuit_sim::bleTrackerFrames().empty(), "tracker empty scan");
  setenv("CROSSPLAY_SIM_TRACKER_SCENARIO", "timeout", 1);
  check(biscuit_sim::bleTrackerFrames().empty(), "tracker timeout scan");
  unsetenv("CROSSPLAY_SIM_TRACKER_SCENARIO");
  unsetenv("CROSSPLAY_SIM_PROBE_SCENARIO");
  auto probes = biscuit_sim::probeFrames();
  check(probes.size() == 6, "six synthetic management frame samples");
  biscuit_sim::ParsedProbe parsed;
  check(biscuit_sim::parseProbe(probes[0].bytes.data(),probes[0].bytes.size(),parsed),
        "parse valid directed probe");
  check(parsed.ssid == "LabNetwork" && parsed.mac[0] == 0x02 && parsed.mac[5] == 0x31,
        "extract real SSID and local MAC");
  check(biscuit_sim::parseProbe(probes[1].bytes.data(),probes[1].bytes.size(),parsed)
        && parsed.ssid.empty(), "broadcast probe has empty SSID");
  check(biscuit_sim::parseProbe(probes[2].bytes.data(),probes[2].bytes.size(),parsed)
        && parsed.ssid == "GuestLab", "second directed probe");
  check(probes[0].bytes == probes[3].bytes, "duplicate probe fixture");
  check(!biscuit_sim::parseProbe(probes[4].bytes.data(),probes[4].bytes.size(),parsed),
        "beacon frame cannot be counted as probe");
  check(!biscuit_sim::parseProbe(probes[5].bytes.data(),probes[5].bytes.size(),parsed),
        "truncated tagged parameter rejected");
  check(!biscuit_sim::parseProbe(nullptr,0,parsed), "null packet rejected");
  auto invalid = probes[0].bytes;
  invalid[25] = 33;
  check(!biscuit_sim::parseProbe(invalid.data(),invalid.size(),parsed), "SSID length limited");
  invalid = probes[0].bytes;
  invalid[24] = 42;
  check(!biscuit_sim::parseProbe(invalid.data(),invalid.size(),parsed), "unknown tag rejected");
  setenv("CROSSPLAY_SIM_PROBE_SCENARIO","malformed",1);
  auto malformed = biscuit_sim::probeFrames();
  check(malformed.size()==2 &&
        !biscuit_sim::parseProbe(malformed[0].bytes.data(),malformed[0].bytes.size(),parsed) &&
        !biscuit_sim::parseProbe(malformed[1].bytes.data(),malformed[1].bytes.size(),parsed),
        "malformed-only scenario never produces a probe");
  setenv("CROSSPLAY_SIM_PROBE_SCENARIO","empty",1);
  check(biscuit_sim::probeFrames().empty(), "empty probe scenario");
  unsetenv("CROSSPLAY_SIM_PROBE_SCENARIO");
  unsetenv("CROSSPLAY_SIM_PROBE_SCENARIO");
  std::set<std::string> uniqueMacs;
  for (const auto& p : biscuit_sim::probeFrames()) {
    biscuit_sim::ParsedProbe probe;
    if (biscuit_sim::parseProbe(p.bytes.data(), p.bytes.size(), probe)) {
      uniqueMacs.insert(std::string(reinterpret_cast<const char*>(probe.mac), 6));
    }
  }
  check(uniqueMacs.size() == 3, "crowd density deduplicates repeated MACs");
  setenv("CROSSPLAY_SIM_PROBE_SCENARIO", "empty", 1);
  uniqueMacs.clear();
  for (const auto& p : biscuit_sim::probeFrames()) {
    biscuit_sim::ParsedProbe probe;
    if (biscuit_sim::parseProbe(p.bytes.data(),p.bytes.size(),probe))
      uniqueMacs.insert(std::string(reinterpret_cast<const char*>(probe.mac),6));
  }
  check(uniqueMacs.empty(), "crowd empty fixture has no MACs");
  setenv("CROSSPLAY_SIM_PROBE_SCENARIO", "malformed", 1);
  uniqueMacs.clear();
  for (const auto& p : biscuit_sim::probeFrames()) {
    biscuit_sim::ParsedProbe probe;
    if (biscuit_sim::parseProbe(p.bytes.data(),p.bytes.size(),probe))
      uniqueMacs.insert(std::string(reinterpret_cast<const char*>(probe.mac),6));
  }
  check(uniqueMacs.empty(), "crowd malformed fixture cannot increase count");
  unsetenv("CROSSPLAY_SIM_PROBE_SCENARIO");
  unsetenv("CROSSPLAY_SIM_FINGERPRINT_SCENARIO");
  auto fingerprints = biscuit_sim::fingerprintFrames();
  check(fingerprints.size() == 11, "fingerprinting fixture has nine valid and two invalid frames");
  std::map<std::string, int> fingerprintCounts;
  biscuit_sim::ParsedProbe parsedFingerprint;
  for (const auto& frame : fingerprints) {
    if (biscuit_sim::parseProbe(frame.bytes.data(), frame.bytes.size(),
                               parsedFingerprint))
      ++fingerprintCounts[std::string(reinterpret_cast<const char*>(parsedFingerprint.mac), 6)];
  }
  check(fingerprintCounts.size() == 5, "fingerprint fixture has five unique MACs");
  check(fingerprintCounts[std::string("\x02\x00\x00\x00\x00\x41", 6)] == 2,
        "iOS-like local address has two probes");
  check(fingerprintCounts[std::string("\x06\x00\x00\x00\x00\x42", 6)] == 4,
        "Android-like local address has four probes");
  check(fingerprintCounts[std::string("\x00\x1b\x21\x00\x00\x43", 6)] == 1,
        "Intel OUI fixture present");
  check(fingerprintCounts[std::string("\xb8\x27\xeb\x00\x00\x44", 6)] == 1,
        "Raspberry Pi OUI fixture present");
  setenv("CROSSPLAY_SIM_FINGERPRINT_SCENARIO", "empty", 1);
  check(biscuit_sim::fingerprintFrames().empty(), "fingerprint empty scenario");
  setenv("CROSSPLAY_SIM_FINGERPRINT_SCENARIO", "malformed", 1);
  const auto invalidFingerprints = biscuit_sim::fingerprintFrames();
  check(invalidFingerprints.size() == 2, "fingerprint malformed fixture includes two frames");
  bool anyValidFingerprint = false;
  for (const auto& frame : invalidFingerprints)
    anyValidFingerprint |= biscuit_sim::parseProbe(frame.bytes.data(),
                                                     frame.bytes.size(), parsedFingerprint);
  check(!anyValidFingerprint, "invalid fingerprint frames cannot inflate device list");
  unsetenv("CROSSPLAY_SIM_FINGERPRINT_SCENARIO");
  std::printf("biscuit-sim network: %d checks, 0 failed\n", checks);
}
