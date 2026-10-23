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
):
    assert f"-<apps_local/{path}>" not in ini, f"{cls} excluded from native build"
    assert f"HWSTUB_ACTIVITY({cls})" not in noops, f"{cls} still a no-op"
    assert f'{{"{name}"' in shelf, f"{name} missing from the app list"
    assert f'#include "{path.replace(".cpp", ".h")}"' in shelf, f"{cls} header unavailable in simulator"
    assert (root / "src/apps_local" / path).exists(), f"real {cls} missing"
    blocked_sections = [
        chunk.split("#endif  // !SIMULATOR", 1)[0]
        for chunk in shelf.split("#if !defined(SIMULATOR)")[1:]
    ]
    assert len(blocked_sections) >= 2, "missing hardware-only include/shelf guards"
    assert not any(
        f'#include "{path.replace(".cpp", ".h")}"' in section
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
assert '1600:$frame;4300:$out/${name}_stopwatch.bmp' in smoke_script, "Clock golden must precede mode-switch input"
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


workflow = (root / ".github/workflows/crossplay-ci.yml").read_text()
assert 'bash scripts_local/biscuit_sim_smoke.sh' in workflow, "smoke test not gated by CI"
assert 'test_visual_goldens.py .pio/biscuit-ui-smoke' in workflow, "real screenshot goldens must gate CI"
assert (root / "host-tests/biscuit-sim/visual_goldens.json").exists(), "visual baselines missing"
assert (root / "host-tests/biscuit-preview/BitmapRenderer.h").exists(), "Biscuit X3 preview renderer missing"
assert (root / "host-tests/biscuit-preview/run.sh").exists(), "Biscuit native preview suite missing"
checks += 9
print(f"biscuit-sim wiring: {checks} checks, 0 failed")
