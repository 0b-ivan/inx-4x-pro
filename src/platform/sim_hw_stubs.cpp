// Simulator-only: provides vtables and no-op virtual method bodies for all
// hardware-only activities whose .cpp files are excluded via build_src_filter.
// Shelf.cpp includes their headers unconditionally, so the vtables must exist
// somewhere — this is that somewhere.
//
// Activities that can now simulate (WiFi scan, softAP, Clock, Device Info,
// OTP Generator and display-only apps) are NOT
// listed here — their real .cpp files compile in the simulator build.
#ifdef SIMULATOR

#include "../apps_local/ble-contact/BleContactExchangeActivity.h"
#include "../apps_local/ble-proximity/BleProximityActivity.h"
#include "../apps_local/ble-scanner/BleScannerActivity.h"
#include "../apps_local/fire/FireActivity.h"
#include "../apps_local/host-scanner/HostScannerActivity.h"
#include "../apps_local/http-client/HttpClientActivity.h"
#include "../apps_local/mdns-browser/MdnsBrowserActivity.h"

#define HWSTUB_ACTIVITY(cls)                              \
  void cls::onEnter() {}                                  \
  void cls::onExit() {}                                   \
  void cls::loop() {}                                     \
  void cls::render(RenderLock&&) {}

HWSTUB_ACTIVITY(FireActivity)

#undef HWSTUB_ACTIVITY

// PlayerAuth.cpp is excluded (wolfssl). Provide no-op stubs so
// PlayerService.cpp and PlayerRuntime.cpp link in the simulator.
#include "../apps_local/player/PlayerAuth.h"

namespace player {

bool PlayerAuth::validPin(const char* /*pin*/) { return false; }

PinHashResult PlayerAuth::createCredential(const char* /*pin*/, PinCredential& /*out*/) {
  return PinHashResult::CryptoError;
}

AuthResult PlayerAuth::authenticate(const PlayerId& /*playerId*/, const char* /*pin*/) {
  return AuthResult::CryptoError;
}

}  // namespace player

#endif // SIMULATOR
