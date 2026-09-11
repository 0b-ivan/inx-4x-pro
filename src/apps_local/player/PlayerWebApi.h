#pragma once

#include <string>

namespace playerweb {

struct Reply {
  int status = 200;
  std::string body;
};

// Browser-facing adapter around the device-wide PlayerRuntime. The browser
// never receives PIN hashes/salts and never supplies stats, XP, rank or radar.
// All authoritative state remains on the X4.
Reply state();
Reply useGuest();
Reply login(const char* playerIdHex, const char* pin);
Reply registerGuest(const char* name, const char* pin);
Reply stepGuestCallsign(int slot);

// Visible identity used by game transports. Registered profiles expose their
// chosen player name; guests expose the generated callsign instead of a generic
// "X4 PRO" label.
std::string displayName();

}  // namespace playerweb
