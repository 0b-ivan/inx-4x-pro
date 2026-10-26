#include "../../../src/apps_local/player/PlayerWebApi.h"

namespace playerweb {

Reply state() { return {200, "{\"ok\":true}"}; }
Reply useGuest() { return {200, "{\"ok\":true}"}; }
Reply login(const char*, const char*) { return {200, "{\"ok\":true}"}; }
Reply registerGuest(const char*, const char*) { return {200, "{\"ok\":true}"}; }
Reply stepGuestCallsign(int) { return {200, "{\"ok\":true}"}; }
std::string displayName() { return "SPIKY WINK BEARD"; }

}  // namespace playerweb
