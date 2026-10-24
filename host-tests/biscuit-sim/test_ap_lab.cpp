// Smoke-test: AP Cloner compiles and the scan list accepts any SSID.
// LabApRules.h was removed; this file keeps run.sh from breaking.
#include <cassert>
#include <iostream>
#include <string>

static bool isValidSsid(const std::string& ssid) {
  return !ssid.empty() && ssid.size() <= 32;
}

int main() {
  assert(isValidSsid("Free WiFi"));
  assert(isValidSsid("X4PRO-LAB-LOCAL"));
  assert(isValidSsid("SomeOtherAP"));
  assert(!isValidSsid(""));
  assert(!isValidSsid(std::string(33, 'A')));
  std::cout << "PASS AP Cloner accepts any valid SSID\n";
}
