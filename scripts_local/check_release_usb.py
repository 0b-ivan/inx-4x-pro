"""Validate the resolved PlatformIO release configuration before building."""
import json
import re
import sys


def check(config):
    sections = dict(config)
    release = dict(sections.get("env:gh_release_x4pro", []))
    flags = " ".join(release.get("build_flags", []))
    for name in ("ARDUINO_USB_MODE", "ARDUINO_USB_CDC_ON_BOOT"):
        values = re.findall(r"(?:^|\s)-D\s*" + name + r"=([^\s]+)", flags)
        if values != ["1"] or re.search(r"(?:^|\s)-U\s*" + name + r"(?:\s|$)", flags):
            raise ValueError(f"{name} must be defined exactly once as 1 for standard USB serial")
    for key in ("extra_scripts", "lib_deps"):
        if any(re.search(r"passkey|fido|ctap", value, re.I) for value in release.get(key, [])):
            raise ValueError(f"Passkey/FIDO component found in release {key}")


if __name__ == "__main__":
    try:
        check(json.load(sys.stdin))
    except (ValueError, TypeError) as error:
        sys.exit(f"Release USB check failed: {error}")
    print("Release USB: hardware Serial/JTAG and CDC on boot enabled; no Passkey/FIDO components")
