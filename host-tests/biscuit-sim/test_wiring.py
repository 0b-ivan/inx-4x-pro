#!/usr/bin/env python3
"""Guard against a green simulator that silently swaps the new apps back to no-ops."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
ini = (root / "platformio.sim.ini").read_text()
noops = (root / "src/platform/sim_hw_stubs.cpp").read_text()
shelf = (root / "src/apps_local/Shelf.cpp").read_text()
clock = (root / "src/apps_local/clock/ClockActivity.cpp").read_text()
info = (root / "src/apps_local/device-info/DeviceInfoActivity.cpp").read_text()
checks = 0
for name, cls, path in (
    ("CLOCK", "ClockActivity", "clock/ClockActivity.cpp"),
    ("DEVICE INFO", "DeviceInfoActivity", "device-info/DeviceInfoActivity.cpp"),
    ("OTP GENERATOR", "OtpGeneratorActivity", "otp-generator/OtpGeneratorActivity.cpp"),
    ("DNS LOOKUP", "DnsLookupActivity", "dns-lookup/DnsLookupActivity.cpp"),
    ("WIFI CONNECT", "WifiConnectActivity", "wifi-connect/WifiConnectActivity.cpp"),
    ("PING", "PingActivity", "ping/PingActivity.cpp"),
    ("HOST SCANNER", "HostScannerActivity", "host-scanner/HostScannerActivity.cpp"),
    ("HTTP CLIENT", "HttpClientActivity", "http-client/HttpClientActivity.cpp"),
    ("MDNS BROWSER", "MdnsBrowserActivity", "mdns-browser/MdnsBrowserActivity.cpp"),
    ("BLE PROXIMITY", "BleProximityActivity", "ble-proximity/BleProximityActivity.cpp"),
    ("BLE SCANNER", "BleScannerActivity", "ble-scanner/BleScannerActivity.cpp"),
    ("BLE CONTACT", "BleContactExchangeActivity", "ble-contact/BleContactExchangeActivity.cpp"),
    ("TRACKER DETECT", "TrackerDetectorActivity", "tracker-detector/TrackerDetectorActivity.cpp"),
    ("PROBE SNIFFER", "ProbeSnifferActivity", "probe-sniffer/ProbeSnifferActivity.cpp"),
    ("CROWD DENSITY", "CrowdDensityActivity", "crowd-density/CrowdDensityActivity.cpp"),
    ("DEV FINGERPRINT", "DeviceFingerprinterActivity", "device-fingerprinter/DeviceFingerprinterActivity.cpp"),
    ("DEAUTH DETECTOR", "DeauthDetectorActivity", "deauth-detector/DeauthDetectorActivity.cpp"),
    ("PACKET MONITOR", "PacketMonitorActivity", "packet-monitor/PacketMonitorActivity.cpp"),
    ("NETWORK MONITOR", "NetworkMonitorActivity", "network-monitor/NetworkMonitorActivity.cpp"),
    ("BLE BEACON", "BleBeaconActivity", "ble-beacon/BleBeaconActivity.cpp"),
    ("AIRTAG SPOOF", "AirTagTestActivity", "airtag-test/AirTagTestActivity.cpp"),
    ("WIFI DEAUTH", "WifiTestActivity", "wifi-test/WifiTestActivity.cpp"),
    ("BLE SPAM", "BleSpamActivity", "ble-spam/BleSpamActivity.cpp"),
    ("BLE KEYBOARD", "BleKeyboardActivity", "ble-keyboard/BleKeyboardActivity.cpp"),
    ("MESH CHAT", "MeshChatActivity", "mesh-chat/MeshChatActivity.cpp"),
    ("FIRE", "FireActivity", "fire/FireSimActivity.cpp"),
):
    assert f"-<apps_local/{path}>" not in ini, f"{cls} excluded from native build"
    assert f"HWSTUB_ACTIVITY({cls})" not in noops, f"{cls} still a no-op"
    assert f'{{"{name}"' in shelf, f"{name} missing from the app list"
    header = "fire/FireActivity.h" if cls == "FireActivity" else path.replace(".cpp", ".h")
    assert f'#include "{header}"' in shelf, f"{cls} header unavailable in simulator"
    assert (root / "src/apps_local" / path).exists(), f"Activity implementation missing: {cls}"
    # Once all 26 entries are simulator-visible, no hardware-only Shelf
    # sections are required. If one exists, it must not hide an enabled app.
    blocked_sections = [
        chunk.split("#endif  // !SIMULATOR", 1)[0]
        for chunk in shelf.split("#if !defined(SIMULATOR)")[1:]
    ]
    assert not any(
        f'#include "{header}"' in section
        or f'{{"{name}"' in section
        for section in blocked_sections
    ), f"{cls} remains hidden behind !SIMULATOR guard"
    checks += 5
assert "biscuit_sim::readClockUtc" in clock, "clock does not use simulated time"
assert "simulated X4 Pro" in info, "device info reports false ESP metrics"
assert "snprintf(ipBuf" in info, "native String must not require toCharArray"
dns = (root / "src/apps_local/dns-lookup/DnsLookupActivity.cpp").read_text()
assert "biscuit_sim::resolveHost" in dns, "DNS must use isolated host resolver"
assert "biscuit_sim::tcpProbe" in (root / "src/apps_local/ping/PingActivity.cpp").read_text(), "Ping must remain isolated"
ble = (root / "src/apps_local/ble-proximity/BleProximityActivity.cpp").read_text()
assert "biscuit_sim::bleAdvertisements()" in ble, "BLE proximity must consume virtual BLE fixtures"
assert "#ifndef SIMULATOR" in ble, "BLE radio access must be hardware-only"
assert (root / "src/platform/sim_biscuit_ble.h").exists(), "BLE simulator fixtures missing"
scanner = (root / "src/apps_local/ble-scanner/BleScannerActivity.cpp").read_text()
assert "biscuit_sim::bleGattServices" in scanner and "biscuit_sim::bleAdvertisements()" in scanner, "BLE scanner must use fixture-backed real activity"
assert "biscuit_sim::bleGattCharacteristics" in scanner and "biscuit_sim::bleConnects" in scanner, "BLE GATT must be isolated"
contact = (root / "src/apps_local/ble-contact/BleContactExchangeActivity.cpp").read_text()
assert "biscuit_sim::bleContactPackets()" in contact, "BLE Contact must use isolated frames"
assert "simContactTrace" in contact, "BLE Contact needs state evidence"
tracker = (root / "src/apps_local/tracker-detector/TrackerDetectorActivity.cpp").read_text()
assert "biscuit_sim::bleTrackerFrames()" in tracker, "tracker must consume isolated advertisements"
assert "trackerSimTrace" in tracker and "identifyTracker(" in tracker, "tracker must execute real classification"
assert "#ifndef SIMULATOR" in tracker, "real BLE hardware must be disabled natively"
probe = (root / "src/apps_local/probe-sniffer/ProbeSnifferActivity.cpp").read_text()
assert "biscuit_sim::probeFrames()" in probe and "biscuit_sim::parseProbe" in probe, "probe sniffer must use isolated frames"
assert "onProbeRequest(parsed.mac" in probe, "native frames must use real dedup handler"
assert "#ifndef SIMULATOR" in probe, "hardware-only raw packet callback missing guard"
assert (root / "src/platform/sim_biscuit_wifi_frames.h").exists(), "virtual 802.11 fixtures missing"
crowd = (root / "src/apps_local/crowd-density/CrowdDensityActivity.cpp").read_text()
assert "biscuit_sim::probeFrames()" in crowd and "biscuit_sim::parseProbe" in crowd, "crowd density must use isolated probe frames"
assert "addMac(parsed.mac)" in crowd, "crowd counting must reuse real MAC dedup"
assert "simFedWindow" in crowd and "crowdSimTrace" in crowd, "simulated sampling/cleanup evidence missing"
assert "#ifndef SIMULATOR" in crowd, "raw capture remains outside the simulator"
fingerprinter = (root / "src/apps_local/device-fingerprinter/DeviceFingerprinterActivity.cpp").read_text()
assert "biscuit_sim::fingerprintFrames()" in fingerprinter, "fingerprinter must use isolated frames"
assert "biscuit_sim::parseProbe" in fingerprinter and "onProbePacket(frame.bytes.data()" in fingerprinter, "real fingerprinter handler must be exercised"
assert "fingerprintSimTrace" in fingerprinter and '#ifndef SIMULATOR' in fingerprinter, "fingerprinter radio isolation missing"
deauth = (root / "src/apps_local/deauth-detector/DeauthDetectorActivity.cpp").read_text()
assert "biscuit_sim::deauthMonitorFrames()" in deauth, "Deauth Detector must consume isolated management frames"
assert "onPacket(frame.bytes.data()" in deauth, "real event ring-buffer handler must be executed"
assert "deauthSimTrace" in deauth and "#ifndef SIMULATOR" in deauth, "physical radio must be guarded"
assert "esp_wifi_set_promiscuous" in deauth, "hardware build must keep existing radio behavior"
packet = (root / "src/apps_local/packet-monitor/PacketMonitorActivity.cpp").read_text()
assert "biscuit_sim::packetMonitorFrames()" in packet, "Packet Monitor must consume synthetic 802.11 frames"
assert "onPacket(frame.bytes.data()" in packet, "real Packet Monitor counters must be exercised"
assert "#ifndef SIMULATOR" in packet and "packetSimTrace" in packet, "native radio isolation and test traces required"
netmon = (root / "src/apps_local/network-monitor/NetworkMonitorActivity.cpp").read_text()
assert "biscuit_sim::deauthMonitorFrames()" in netmon, "Network Monitor must consume in-memory management frames"
assert "onFrame(frame.bytes.data()" in netmon, "Network Monitor must execute original event grouping"
assert "networkSimTrace" in netmon and "#ifndef SIMULATOR" in netmon, "Network Monitor must isolate radio calls"
assert "analyzeGroups()" in netmon and "rogue-results" in netmon, "Network Monitor must exercise rogue AP grouping"
for filename, trace in (
    ("ble-beacon/BleBeaconActivity.cpp", "beaconSimTrace"),
    ("airtag-test/AirTagTestActivity.cpp", "airtagSimTrace"),
):
    code = (root / "src/apps_local" / filename).read_text()
    assert "biscuit_sim::blePreview(" in code, f"{filename}: no non-radio preview packet"
    assert trace in code and "#ifndef SIMULATOR" in code, f"{filename}: radio isolation missing"
wifi_test = (root / "src/apps_local/wifi-test/WifiTestActivity.cpp").read_text()
assert "biscuit_sim::deauthPreviewAps()" in wifi_test, "WiFi Deauth must use virtual AP list"
assert "OFFLINE SIMULATION - NO TX" in wifi_test and "wifiDeauthSimTrace" in wifi_test, "WiFi Deauth UI must flag dry-run and emit test state"
assert "esp_wifi_80211_tx" in wifi_test and "#ifndef SIMULATOR" in wifi_test, "real transmit must remain hardware-only"
spam = (root / "src/apps_local/ble-spam/BleSpamActivity.cpp").read_text()
assert "biscuit_sim::blePreview(" in spam and "spamSimTrace" in spam, "BLE Spam must use isolated preview data"
assert "#ifndef SIMULATOR" in spam and "Simulated previews" in spam, "BLE Spam must show simulated-only mode"
keyboard = (root / "src/apps_local/ble-keyboard/BleKeyboardActivity.cpp").read_text()
assert "keyboardSimTrace" in keyboard and "simKeys" in keyboard, "virtual HID backend missing"
assert "executeCurrentLine()" in keyboard and "executeLine(scriptLines[currentLine])" in keyboard, "real script parser must execute"
assert "#ifndef SIMULATOR" in keyboard and "if (!deviceConnected || keyCode == 0) return;" in keyboard, "host input isolation missing"
smoke_script = (root / "scripts_local/biscuit_sim_smoke.sh").read_text()
assert 'CROSSPLAY_AUTOSTART' in smoke_script, "CI smoke must open real apps"
assert 'CROSSPOINT_SIM_SCREENSHOTS' in smoke_script, "CI smoke must capture pixels"
assert 'CROSSPOINT_SIM_INPUT_SCRIPT' in smoke_script, "CI smoke must inject input"
assert 'TAP:100,210,120' in smoke_script, "X4 Pro native touch row hit test must be covered"
assert 'TAP:180,780,120' in smoke_script, "X4 Pro on-screen Select hint must be tested"
assert 'PROBE:touch-detail:3' in smoke_script, "X4 Pro touch path must prove real Activity navigation"
assert 'script="8500:ENTER:650;11800:QUIT"' in smoke_script, "Clock mode-switch must happen after stable golden"
assert 'shots="6500:$frame;10400:$out/${name}_stopwatch.bmp"' in smoke_script, "Clock golden must show actual Clock after boot, before Stopwatch"
assert 'Entering activity: Clock' in smoke_script, "Clock must reject boot-splash screenshots"
assert "PASS Biscuit 26/26: screenshots contain rendered Activity headers" in smoke_script, (
    "all 26 Activity snapshots must be checked against boot-splash false positives")
crowd_ui = (root / "src/apps_local/crowd-density/CrowdDensityActivity.cpp").read_text()
assert "const int footerHeight = BaseMetrics::values.buttonHintsHeight;" in crowd_ui, (
    "Crowd Density must reserve its forced touch footer height")
assert "const int chartBottom = noteY - 12;" in crowd_ui, (
    "Crowd Density chart must not draw across the disclaimer")
assert "MAC randomization reduces accuracy." in crowd_ui and "noteY" in crowd_ui, (
    "Crowd Density disclaimer must stay above the touch hints")

assert 'TAP:420,780,120' in smoke_script and 'MESH:touch-relay:2:2' in smoke_script, "Mesh Chat X4 Pro touch E2E missing"
assert 'CLOCK:mode:stopwatch' in smoke_script, "Clock long-hold mode transition must be checked"

assert 'NETWORK_MONITOR_touch_detail.bmp' in smoke_script, "X4 Pro Network Monitor touch E2E missing"
# Activities using drawButtonHintsAlways() must not hit-test using the
# touch-theme's intentionally zero-height normal button bar.
touch_footer_activities = (
    "wifi-connect/WifiConnectActivity.cpp",
    "dns-lookup/DnsLookupActivity.cpp",
    "host-scanner/HostScannerActivity.cpp",
    "http-client/HttpClientActivity.cpp",
    "mdns-browser/MdnsBrowserActivity.cpp",
    "ble-proximity/BleProximityActivity.cpp",
    "ble-scanner/BleScannerActivity.cpp",
    "ble-contact/BleContactExchangeActivity.cpp",
    "tracker-detector/TrackerDetectorActivity.cpp",
    "probe-sniffer/ProbeSnifferActivity.cpp",
    "crowd-density/CrowdDensityActivity.cpp",
    "deauth-detector/DeauthDetectorActivity.cpp",
    "packet-monitor/PacketMonitorActivity.cpp",
    "network-monitor/NetworkMonitorActivity.cpp",
    "ble-beacon/BleBeaconActivity.cpp",
    "airtag-test/AirTagTestActivity.cpp",
    "wifi-test/WifiTestActivity.cpp",
    "ble-spam/BleSpamActivity.cpp",
    "ble-keyboard/BleKeyboardActivity.cpp",
)
for rel in touch_footer_activities:
    content = (root / "src/apps_local" / rel).read_text()
    assert "GUI.drawButtonHintsAlways(" in content, f"{rel}: touch footer not actually drawn"
    assert "GUI.tappedAlwaysHintButton(" in content, f"{rel}: touch footer hit testing missing"
    assert "GUI.tappedHintButton(" not in content, f"{rel}: still using zero-height touch hitbox"


# Touch-screen navigation hints belong to the Biscuit Activities only.
# Shelf and original CrossPlay games/apps provide their own FreeInkUI touch
# controls. A forced legacy footer overlays their last row (X4 Pro HW report).
shelf_folder = (root / "src/apps_local/ShelfFolderActivity.cpp").read_text()
assert "drawButtonHintsAlways(" not in shelf_folder, "Shelf folder paints footer over touch rows"
assert "tappedAlwaysHintButton(" not in shelf_folder, "Shelf folder consumes Biscuit footer hitboxes"

legacy_app_roots = (
    "battleship", "checkers", "chess", "connectfour", "connections",
    "dungeon", "forehead", "hackernews", "insider", "instapaper",
    "jaipur", "knucklebones", "link", "minesweeper", "murdle",
    "player", "sample", "seasalt", "solitaire", "study", "sudoku",
    "tarot", "totp", "toybattle", "trivia", "wavelength",
    "wifi-scanner", "xkcd", "yahtzee",
)
for folder in legacy_app_roots:
    for path in (root / "src/apps_local" / folder).rglob("*.cpp"):
        code = path.read_text()
        assert "GUI.drawButtonHintsAlways(" not in code, (
            f"{path.relative_to(root)}: forced Biscuit footer in a legacy screen"
        )
        assert "GUI.tappedAlwaysHintButton(" not in code, (
            f"{path.relative_to(root)}: invisible forced-footer touch target"
        )

# Normal legacy hints must remain disabled on touch-capable boards in all
# supported themes, while Biscuit's explicit 'Always' method stays available.
for theme in (
    "BaseTheme.cpp",
    "lyra/LyraTheme.cpp",
    "roundedraff/RoundedRaffTheme.cpp",
):
    code = (root / "src/components/themes" / theme).read_text()
    normal_hint = code.index("::drawButtonHints(")
    guard = code[normal_hint:normal_hint + 300]
    assert "BoardConfig::hasTouch()" in guard, f"{theme}: touch hints no longer suppressed"


# Additional Biscuit apps reported unusable on real GT911 hardware.
# An always-painted footer must NEVER be paired with tappedHintButton(),
# because UITheme sets normal hint height to zero when touch is enabled.
for rel in (
    "ap-cloner/ApClonerActivity.cpp",
    "captive-portal/CaptivePortalActivity.cpp",
    "bulletin-board/BulletinBoardActivity.cpp",
    "dead-drop/DeadDropActivity.cpp",
    "wardriving/WardrivingActivity.cpp",
):
    content = (root / "src/apps_local" / rel).read_text()
    assert "GUI.drawButtonHintsAlways(" in content, f"{rel}: no Biscuit controls"
    assert "GUI.tappedAlwaysHintButton(" in content, f"{rel}: GT911 footer ignores touch"
    assert "GUI.tappedHintButton(" not in content, f"{rel}: zero-height touch hitbox"
    assert "BaseMetrics::values.buttonHintsHeight" in content, (
        f"{rel}: touches or list rows can overlap always-visible footer"
    )
board = (root / "src/apps_local/bulletin-board/BulletinBoardActivity.cpp").read_text()
drop = (root / "src/apps_local/dead-drop/DeadDropActivity.cpp").read_text()
for title, code in (("Bulletin Board", board), ("Dead Drop", drop)):
    assert "mappedInput.rowTouch(" in code, f"{title}: list cannot be tapped directly"
assert 'Storage.mkdir("/crossplay");' in drop, "Dead Drop must create the parent upload directory"
wardrive = (root / "src/apps_local/wardriving/WardrivingActivity.cpp").read_text()
assert "if (!scanPending) return;" in wardrive and "if (!scanPending &&" in wardrive, (
    "Wardriving must process async results only for an outstanding scan")
ap_lab = (root / "src/apps_local/ap-cloner/ApClonerActivity.cpp").read_text()
portal = (root / "src/apps_local/captive-portal/CaptivePortalActivity.cpp").read_text()

# AP Cloner: clones any AP from the scan list — no lab-only guard.
assert 'aps.push_back(std::move(ap));' in ap_lab, "AP Cloner must show all scan results"
assert 'WiFi.softAP(clonedSsid.c_str()' in ap_lab, "AP Cloner must start hardware SoftAP"
assert 'btn == 3 && count > 0' in ap_lab and 'btn == 4 && count > 0' in ap_lab, (
    "AP Cloner GT911 up/down footer actions are not wired"
)
assert 'ap_lab::isLabSsid' not in ap_lab, "AP Cloner must not block arbitrary SSIDs"
# Evil Portal: live AP/DNS/HTTP with credential capture.
assert 'std::get<KeyboardResult>(result.data)' in portal, "Portal SSID keyboard missing"
assert 'webServer->on("/login", HTTP_POST' in portal, "Portal login route missing"
assert 'WiFi.softAP(apSsid.c_str())' in portal and 'dnsServer->start(53' in portal, (
    "Portal must initialize real AP and DNS servers"
)
assert 'GUI.tappedAlwaysHintButton(' in portal, "Portal needs GT911 touch handling"
assert 'logCredentials(' in portal, "Portal must log captured credentials"
assert 'creds.csv' in portal, "Portal must write credentials to SD"

# Host Scanner regression: TCP connection deadlines must not silently fall
# back to the Arduino network client's 3-second default, and leaving a
# scanner must never disconnect the Wi-Fi session it borrowed.
host_scan = (root / "src/apps_local/host-scanner/HostScannerActivity.cpp").read_text()
assert "c.connect(a, port, 180)" in host_scan, "Host discovery TCP connect timeout not bounded"
assert "client.connect(addr, port, 500)" in host_scan, "Port scan TCP connect timeout not bounded"
assert "RADIO.shutdown()" not in host_scan, "Host Scanner must preserve existing Wi-Fi association"
assert "const bool back = mappedInput.wasReleased(" in host_scan, "Host Scanner touch Back missing"
assert "BaseMetrics::values.buttonHintsHeight" in host_scan, "Host Scanner list overlaps touch footer"
assert 'if (btn == 3)' in host_scan and 'if (btn == 4)' in host_scan, (
    "Host Scanner touch CSV and rescan actions not wired"
)
print("Host Scanner TCP deadlines, radio ownership and GT911 controls: 6 checks, 0 failed")

workflow = (root / ".github/workflows/crossplay-ci.yml").read_text()
assert 'bash scripts_local/biscuit_sim_smoke.sh' in workflow, "smoke test not gated by CI"
assert 'test_visual_goldens.py .pio/biscuit-ui-smoke' in workflow, "real screenshot goldens must gate CI"
assert (root / "host-tests/biscuit-sim/visual_goldens.json").exists(), "visual baselines missing"
assert (root / "host-tests/biscuit-preview/BitmapRenderer.h").exists(), "Biscuit X3 preview renderer missing"
assert (root / "host-tests/biscuit-preview/run.sh").exists(), "Biscuit native preview suite missing"
checks += 9
print(f"biscuit-sim wiring: {checks} checks, 0 failed")

mesh = (root / "src/apps_local/mesh-chat/MeshChatActivity.cpp").read_text()
mesh_sim = (root / "sim-stubs/esp_now.h").read_text()
assert "esp_now_sim_deliver" in mesh and "esp_now_sim_deliver" in mesh_sim, "mesh transport must stay synthetic"
assert "meshSimTrace" in mesh and "radio-off" in mesh, "mesh lifecycle trace missing"
assert "-<apps_local/mesh-chat/MeshChatActivity.cpp>" not in ini, "real Mesh Chat excluded"
assert "HWSTUB_ACTIVITY(MeshChatActivity)" not in noops, "Mesh Chat must not be a vtable stub"

fire_sim = (root / "src/apps_local/fire/FireSimActivity.cpp").read_text()
assert "-<apps_local/fire/FireActivity.cpp>" in ini, "hardware Fire code must remain excluded natively"
assert "FIRE - SIMULATION" in fire_sim, "Fire must clearly indicate UI-only preview"
assert "packetsSent = 0" in fire_sim and "No packets transmitted" in fire_sim, "Fire native simulation must never transmit"
assert "HWSTUB_ACTIVITY(FireActivity)" not in noops, "Fire must execute a real simulator-specific UI"
assert "esp_wifi_80211_tx" not in fire_sim and "esp_now_send" not in fire_sim, "Fire preview must contain no TX APIs"


# Browser boundaries: never treat untrusted messages or filenames as HTML.
board = (root / "src/apps_local/bulletin-board/BulletinBoardActivity.cpp").read_text()
drop = (root / "src/apps_local/dead-drop/DeadDropActivity.cpp").read_text()
for name, app in (("Bulletin Board", board), ("Dead Drop", drop)):
    assert ".innerHTML" not in app, f"{name}: untrusted values rendered as HTML"
    assert "textContent" in app, f"{name}: missing safe browser text rendering"
    assert "local_web::appendJsonQuoted" in app, f"{name}: manual JSON escaping"
assert "local_web::validFileName" in drop, "Dead Drop: file name validation missing"
assert "uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize" in drop, (
    "Dead Drop: partial file writes must fail"
)
assert "File listing exceeds size limit" in drop, "Dead Drop: JSON output needs size bound"
assert "encodeHeaderFileName" in drop, "Dead Drop: Content-Disposition requires encoding"
print("local web wiring: 9 checks, 0 failed")

# GT911 touch footer action regression on X4 Pro, absent from the original X3.
ping = (root / "src/apps_local/ping/PingActivity.cpp").read_text()
finger = (root / "src/apps_local/device-fingerprinter/DeviceFingerprinterActivity.cpp").read_text()
assert ping.count("GUI.tappedAlwaysHintButton(") >= 2, "Ping cannot stop/exit via touch"
assert "GUI.tappedAlwaysHintButton(" in finger and "button == 2" in finger, "Fingerprinter Start tap missing"
assert "mappedInput.rowTouch(" in finger, "Fingerprinter list touch selection missing"
assert "BaseMetrics::values.buttonHintsHeight" in finger, "Fingerprinter list overlaps GT911 footer"
print("biscuit GT911 Ping + Fingerprinter wiring: 4 checks, 0 failed")

# SD regression: uploads are staged, never overwrite existing files on failure.
drop = (root / "src/apps_local/dead-drop/DeadDropActivity.cpp").read_text()
ward = (root / "src/apps_local/wardriving/WardrivingActivity.cpp").read_text()
assert 'Storage.open(uploadTempPath.c_str(), O_WRITE | O_CREAT | O_TRUNC)' in drop
assert 'Storage.rename(uploadTempPath.c_str(), uploadPath.c_str())' in drop
assert 'uploadHttpError = 409;' in drop
assert 'Storage.remove(uploadPath.c_str())' not in drop
assert 'Storage.remove(uploadTempPath.c_str())' in drop
assert 'if (!Storage.writeFile(filename.c_str()' in ward
assert 'wardrive_write::writeComplete' in ward
assert 'if (storageError) break;' in ward
assert 'if (!rawBssid) continue;' in ward
print("storage error paths: 9 checks, 0 failed")
