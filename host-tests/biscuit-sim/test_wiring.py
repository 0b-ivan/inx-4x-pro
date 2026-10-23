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
):
    assert f"-<apps_local/{path}>" not in ini, f"{cls} excluded from native build"
    assert f"HWSTUB_ACTIVITY({cls})" not in noops, f"{cls} still a no-op"
    assert f'{{"{name}"' in shelf, f"{name} missing from the app list"
    assert f'#include "{path.replace(".cpp", ".h")}"' in shelf, f"{cls} header unavailable in simulator"
    assert (root / "src/apps_local" / path).exists(), f"real {cls} missing"
    checks += 5
assert "biscuit_sim::readClockUtc" in clock, "clock does not use simulated time"
assert "simulated X4 Pro" in info, "device info reports false ESP metrics"
checks += 2
print(f"biscuit-sim wiring: {checks} checks, 0 failed")
