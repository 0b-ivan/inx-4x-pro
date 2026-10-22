// Simulator-only: provides vtables and no-op virtual method bodies for all
// hardware-only activities whose .cpp files are excluded via build_src_filter.
// Shelf.cpp includes their headers unconditionally, so the vtables must exist
// somewhere — this is that somewhere.
#ifdef SIMULATOR

#include "../apps_local/airtag-test/AirTagTestActivity.h"
#include "../apps_local/ap-cloner/ApClonerActivity.h"
#include "../apps_local/ap-history-logger/ApHistoryLoggerActivity.h"
#include "../apps_local/barcode/BarcodeActivity.h"
#include "../apps_local/beacon-test/BeaconTestActivity.h"
#include "../apps_local/ble-beacon/BleBeaconActivity.h"
#include "../apps_local/ble-contact/BleContactExchangeActivity.h"
#include "../apps_local/ble-keyboard/BleKeyboardActivity.h"
#include "../apps_local/ble-proximity/BleProximityActivity.h"
#include "../apps_local/ble-scanner/BleScannerActivity.h"
#include "../apps_local/ble-spam/BleSpamActivity.h"
#include "../apps_local/bulletin-board/BulletinBoardActivity.h"
#include "../apps_local/captive-portal/CaptivePortalActivity.h"
#include "../apps_local/clock/ClockActivity.h"
#include "../apps_local/credential-viewer/CredentialViewerActivity.h"
#include "../apps_local/crowd-density/CrowdDensityActivity.h"
#include "../apps_local/dead-drop/DeadDropActivity.h"
#include "../apps_local/deauth-detector/DeauthDetectorActivity.h"
#include "../apps_local/device-fingerprinter/DeviceFingerprinterActivity.h"
#include "../apps_local/device-info/DeviceInfoActivity.h"
#include "../apps_local/dns-lookup/DnsLookupActivity.h"
#include "../apps_local/fire/FireActivity.h"
#include "../apps_local/host-scanner/HostScannerActivity.h"
#include "../apps_local/http-client/HttpClientActivity.h"
#include "../apps_local/key-copier/KeyCopierActivity.h"
#include "../apps_local/mdns-browser/MdnsBrowserActivity.h"
#include "../apps_local/mesh-chat/MeshChatActivity.h"
#include "../apps_local/network-change/NetworkChangeActivity.h"
#include "../apps_local/network-monitor/NetworkMonitorActivity.h"
#include "../apps_local/otp-generator/OtpGeneratorActivity.h"
#include "../apps_local/packet-monitor/PacketMonitorActivity.h"
#include "../apps_local/password-manager/PasswordGeneratorActivity.h"
#include "../apps_local/perimeter-watch/PerimeterWatchActivity.h"
#include "../apps_local/ping/PingActivity.h"
#include "../apps_local/probe-sniffer/ProbeSnifferActivity.h"
#include "../apps_local/security-pin/SecurityPinActivity.h"
#include "../apps_local/signal-triangulation/SignalTriangulationActivity.h"
#include "../apps_local/ssid-channel/SsidChannelActivity.h"
#include "../apps_local/tracker-detector/TrackerDetectorActivity.h"
#include "../apps_local/usb-hid/UsbHidActivity.h"
#include "../apps_local/vehicle-finder/VehicleFinderActivity.h"
#include "../apps_local/vendor-oui/VendorOuiActivity.h"
#include "../apps_local/wardriving/WardrivingActivity.h"
#include "../apps_local/wifi-connect/WifiConnectActivity.h"
#include "../apps_local/wifi-creds/WifiCredsActivity.h"
#include "../apps_local/wifi-heatmap/WifiHeatMapActivity.h"
#include "../apps_local/wifi-test/WifiTestActivity.h"

#define HWSTUB_ACTIVITY(cls)                              \
  void cls::onEnter() {}                                  \
  void cls::onExit() {}                                   \
  void cls::loop() {}                                     \
  void cls::render(RenderLock&&) {}

HWSTUB_ACTIVITY(AirTagTestActivity)
HWSTUB_ACTIVITY(ApClonerActivity)
HWSTUB_ACTIVITY(ApHistoryLoggerActivity)
HWSTUB_ACTIVITY(BarcodeActivity)
HWSTUB_ACTIVITY(BeaconTestActivity)
HWSTUB_ACTIVITY(BleBeaconActivity)
HWSTUB_ACTIVITY(BleContactExchangeActivity)
HWSTUB_ACTIVITY(BleKeyboardActivity)
HWSTUB_ACTIVITY(BleProximityActivity)
HWSTUB_ACTIVITY(BleScannerActivity)
HWSTUB_ACTIVITY(BleSpamActivity)
HWSTUB_ACTIVITY(BulletinBoardActivity)
HWSTUB_ACTIVITY(CaptivePortalActivity)
HWSTUB_ACTIVITY(ClockActivity)
HWSTUB_ACTIVITY(CredentialViewerActivity)
HWSTUB_ACTIVITY(CrowdDensityActivity)
HWSTUB_ACTIVITY(DeadDropActivity)
DeauthDetectorActivity::~DeauthDetectorActivity() {}
HWSTUB_ACTIVITY(DeauthDetectorActivity)
HWSTUB_ACTIVITY(DeviceFingerprinterActivity)
HWSTUB_ACTIVITY(DeviceInfoActivity)
HWSTUB_ACTIVITY(DnsLookupActivity)
HWSTUB_ACTIVITY(FireActivity)
HWSTUB_ACTIVITY(HostScannerActivity)
HWSTUB_ACTIVITY(HttpClientActivity)
void KeyCopierActivity::onEnter() {}
void KeyCopierActivity::loop() {}
void KeyCopierActivity::render(RenderLock&&) {}
HWSTUB_ACTIVITY(MdnsBrowserActivity)
HWSTUB_ACTIVITY(MeshChatActivity)
HWSTUB_ACTIVITY(NetworkChangeActivity)
HWSTUB_ACTIVITY(NetworkMonitorActivity)
HWSTUB_ACTIVITY(OtpGeneratorActivity)
HWSTUB_ACTIVITY(PacketMonitorActivity)
void PasswordGeneratorActivity::onEnter() {}
void PasswordGeneratorActivity::loop() {}
void PasswordGeneratorActivity::render(RenderLock&&) {}
HWSTUB_ACTIVITY(PerimeterWatchActivity)
HWSTUB_ACTIVITY(PingActivity)
HWSTUB_ACTIVITY(ProbeSnifferActivity)
HWSTUB_ACTIVITY(SecurityPinActivity)
HWSTUB_ACTIVITY(SignalTriangulationActivity)
HWSTUB_ACTIVITY(SsidChannelActivity)
HWSTUB_ACTIVITY(TrackerDetectorActivity)
HWSTUB_ACTIVITY(UsbHidActivity)
HWSTUB_ACTIVITY(VehicleFinderActivity)
HWSTUB_ACTIVITY(VendorOuiActivity)
HWSTUB_ACTIVITY(WardrivingActivity)
HWSTUB_ACTIVITY(WifiConnectActivity)
void WifiCredsActivity::onEnter() {}
void WifiCredsActivity::loop() {}
void WifiCredsActivity::render(RenderLock&&) {}
HWSTUB_ACTIVITY(WifiHeatMapActivity)
HWSTUB_ACTIVITY(WifiTestActivity)

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
