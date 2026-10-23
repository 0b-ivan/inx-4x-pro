#include "../../src/apps_local/captive-portal/PortalLabRules.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using portal_lab::ssidFor;
  using portal_lab::validSuffix;
  using portal_lab::validTestId;

  assert(ssidFor("") == "X4PRO-PORTAL");
  assert(ssidFor("DEV-01") == "X4PRO-PORTAL-DEV-01");
  assert(validSuffix("DEV-01"));
  assert(!validSuffix(""));
  assert(!validSuffix("more-than-twelve"));
  assert(!validSuffix("Other WiFi"));
  assert(!validSuffix("WIFI\nFAKE"));
  assert(!validSuffix(std::string("a") + '\0' + "b"));

  assert(validTestId("X4TEST-A1"));
  assert(validTestId("X4TEST-Z9test123"));
  assert(!validTestId("X4TEST-"));
  assert(!validTestId("admin"));
  assert(!validTestId("X4TEST-A B"));
  assert(!validTestId("X4TEST-abc@example.com"));
  assert(!validTestId("X4TEST-abcdef0123456789012"));
  assert(!validTestId("X4TEST-a\n"));
  // Enforce one synthetic field; reject credential-like or extra fields.
  assert(portal_lab::validSyntheticSubmission("test_id", "X4TEST-DEMO1", 1));
  assert(!portal_lab::validSyntheticSubmission("test_id", "X4TEST-DEMO1", 2));
  assert(!portal_lab::validSyntheticSubmission("test_id", "X4TEST-DEMO1", 0));
  assert(!portal_lab::validSyntheticSubmission("username", "X4TEST-DEMO1", 1));
  assert(!portal_lab::validSyntheticSubmission("password", "X4TEST-DEMO1", 1));
  assert(!portal_lab::validSyntheticSubmission("test_id", "real@example.com", 1));
  assert(!portal_lab::validSyntheticSubmission("test_id", "s3cret", 1));

  std::cout << "PASS portal laboratory validation (non-sensitive synthetic IDs only)\n";
}
