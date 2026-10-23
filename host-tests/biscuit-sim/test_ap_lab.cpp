#include "../../src/apps_local/ap-cloner/LabApRules.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using ap_lab::isLabSsid;
  using ap_lab::validChannel;
  assert(std::string(ap_lab::localSsid()) == "X4PRO-LAB-LOCAL");
  assert(ap_lab::localChannel() == 6);
  assert(isLabSsid(ap_lab::localSsid()));
  assert(isLabSsid("X4PRO-LAB-TEST"));
  assert(isLabSsid("X4PRO-LAB-A1"));
  assert(!isLabSsid(""));
  assert(!isLabSsid("X4PRO-LAB-"));
  assert(!isLabSsid("Other-SSID"));
  assert(!isLabSsid("X4PRO-LAB-TEST\nFAKE"));
  assert(!isLabSsid("X4PRO-LAB-TEST SPACE"));
  assert(!isLabSsid("X4PRO-LAB-TEST@example"));
  assert(!isLabSsid(std::string("X4PRO-LAB-A") + '\0' + "B"));
  assert(!isLabSsid("X4PRO-LAB-123456789012345678901234"));
  assert(!validChannel(0));
  assert(validChannel(1));
  assert(validChannel(6));
  assert(validChannel(11));
  assert(validChannel(13));
  assert(!validChannel(14));
  assert(!validChannel(255));
  std::cout << "PASS AP Lab validation (local lab SSIDs only)\n";
}
