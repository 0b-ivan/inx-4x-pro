import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("usb_guard", Path(__file__).resolve().parents[2] / "scripts_local/check_release_usb.py")
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


def configuration(flags=None, **options):
    return [["env:gh_release_x4pro", list(dict(build_flags=flags if flags is not None else [
        "-DARDUINO_USB_MODE=1", "-DARDUINO_USB_CDC_ON_BOOT=1"], **options).items())]]


class UsbRelease(unittest.TestCase):
    def test_standard_serial(self):
        guard.check(configuration())

    def test_hid_usb_mode_rejected(self):
        with self.assertRaises(ValueError):
            guard.check(configuration(["-DARDUINO_USB_MODE=0", "-DARDUINO_USB_CDC_ON_BOOT=1"]))

    def test_disabled_serial_rejected(self):
        with self.assertRaises(ValueError):
            guard.check(configuration(["-DARDUINO_USB_MODE=1", "-DARDUINO_USB_CDC_ON_BOOT=0"]))

    def test_missing_environment_rejected(self):
        with self.assertRaises(ValueError):
            guard.check([])

    def test_overrides_rejected(self):
        for override in ("-DARDUINO_USB_MODE=0", "-UARDUINO_USB_MODE", "-D ARDUINO_USB_MODE=0"):
            with self.subTest(override=override), self.assertRaises(ValueError):
                guard.check(configuration(["-DARDUINO_USB_MODE=1", "-DARDUINO_USB_CDC_ON_BOOT=1", override]))

    def test_passkey_dependencies_rejected(self):
        for key, value in (("extra_scripts", "pre:tools_local/passkey_components.py"), ("lib_deps", "FIDO2")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                guard.check(configuration(**{key: [value]}))


unittest.main()
