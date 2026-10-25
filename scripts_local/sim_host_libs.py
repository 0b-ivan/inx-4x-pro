"""Configure host libraries used by the CrossPlay desktop simulator.

The simulator's MD5Builder uses CommonCrypto on macOS and OpenSSL on Linux.
Linux needs libcrypto explicitly at link time. The native SD encryption and
QR-TOTP activities also use Mbed TLS. CI installs the Linux development
package, but Homebrew's keg-only mbedtls@3 needs explicit include/library
paths on macOS.

Do not use the unversioned Homebrew mbedtls formula: Mbed TLS 4 removed
legacy crypto interfaces used by the firmware. Keep this native-only; the
ESP32 firmware uses its own SDK headers and libraries.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys

Import("env")  # noqa: F821  (SCons injects this)


def setup_macos_mbedtls():
    # Allow non-Homebrew/custom installations without committing machine-local
    # paths to platformio.sim.ini.
    prefix = os.environ.get("CROSSPLAY_MBEDTLS_PREFIX", "").strip()
    if not prefix:
        brew = shutil.which("brew")
        if brew:
            result = subprocess.run(
                [brew, "--prefix", "mbedtls@3"],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode == 0:
                prefix = result.stdout.strip()

    install_hint = (
        "Install the compatible headers and libraries using "
        "'brew install mbedtls@3', or set CROSSPLAY_MBEDTLS_PREFIX "
        "to an existing Mbed TLS 3 installation."
    )
    if not prefix:
        raise RuntimeError("[sim-host-libs] Missing macOS Mbed TLS 3. " + install_hint)

    include_dir = Path(prefix) / "include"
    lib_dir = Path(prefix) / "lib"
    missing_headers = [
        name for name in ("md.h", "aes.h")
        if not (include_dir / "mbedtls" / name).is_file()
    ]
    missing_libs = [
        name for name in ("mbedtls", "mbedcrypto")
        if not any((lib_dir / ("lib" + name + ext)).is_file()
                   for ext in (".dylib", ".a"))
    ]
    if missing_headers or missing_libs:
        raise RuntimeError(
            "[sim-host-libs] Incomplete or incompatible Mbed TLS installation at "
            + str(prefix) + " (missing headers: " + repr(missing_headers)
            + ", missing libs: " + repr(missing_libs) + "). " + install_hint
        )

    env.AppendUnique(  # noqa: F821  (SCons injects this)
        CPPPATH=[str(include_dir)],
        LIBPATH=[str(lib_dir)],
        LINKFLAGS=["-Wl,-rpath," + str(lib_dir)],
    )
    print("[sim-host-libs] macOS Mbed TLS 3: " + str(prefix))


if sys.platform.startswith("linux"):
    env.Append(LIBS=["crypto"])  # noqa: F821
    print("[sim-host-libs] linking libcrypto for the simulator's MD5Builder")
elif sys.platform == "darwin":
    setup_macos_mbedtls()
