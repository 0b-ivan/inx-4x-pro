#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


# Register the app without touching upstream ActivityManager routing.
replace_once(
    "src/apps_local/Shelf.cpp",
    '#include "tarot/TarotActivity.h"\n',
    '#include "tarot/TarotActivity.h"\n#include "totp/TotpActivity.h"\n',
)
replace_once(
    "src/apps_local/Shelf.cpp",
    '  {"RSS READER", &icon_hackernews_32, &RssFeedListActivity::create},\n',
    '  {"RSS READER", &icon_hackernews_32, &RssFeedListActivity::create},\n'
    '    {"AUTHENTICATOR", &icon_study_32, &TotpActivity::create},\n',
)

# The site host-test requires every shelf app to be named in the README. This
# also fixes the pre-existing RSS Reader gate that blocked v1.12.18.
replace_once(
    "README.md",
    '| **Hacker News**  | The front page in a reading serif, articles kept on the card.                |\n',
    '| **Hacker News**  | The front page in a reading serif, articles kept on the card.                |\n'
    '| **RSS Reader**   | Follow RSS feeds directly on the device.                                    |\n'
    '| **Authenticator**| Store multiple TOTP accounts and generate codes offline.                    |\n',
)

# Sticky remains a development target, but releases are X4 Pro only.
replace_once(
    "README.md",
    'CrossPlay is a fork of [CrossPoint](https://crosspointreader.com/) for the\n'
    '**Xteink X4 Pro** and the **Seeed reTerminal Sticky**. CrossPoint turns the\n',
    'CrossPlay is a fork of [CrossPoint](https://crosspointreader.com/) for the\n'
    '**Xteink X4 Pro**. CrossPoint turns the\n',
)
replace_once(
    "README.md",
    'CrossPlay targets two devices: the **Xteink X4 Pro** and the **Seeed\n'
    'reTerminal Sticky**, both ESP32-S3 with the same 800x480 panel and capacitive\n'
    'touch. For every other device CrossPoint supports, CrossPoint upstream is the\n'
    'right answer and is excellent.\n',
    'CrossPlay releases target the **Xteink X4 Pro**. The Seeed reTerminal Sticky\n'
    'environment remains available for development builds, but no Sticky release\n'
    'artifact is published. For every other device CrossPoint supports, CrossPoint\n'
    'upstream is the right answer and is excellent.\n',
)
replace_once(
    "README.md",
    '   `crossplay-<version>-x4pro-full.bin` for the X4 Pro,\n'
    '   `crossplay-<version>-sticky-full.bin` for the Sticky. Each is the whole\n',
    '   `crossplay-<version>-x4pro-full.bin` for the X4 Pro. It is the whole\n',
)
replace_once(
    "README.md",
    '\n   ```bash\n   esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 crossplay-<version>-sticky-full.bin\n   ```\n',
    '\n',
)

# Remove the tag/release environment while keeping env:sticky for development
# CI and local hardware work.
p = Path("platformio.ini")
text = p.read_text()
start = text.find('; What a tag builds for the Sticky, alongside gh_release_x4pro.')
end = text.find('; --- Xteink X4 Pro', start)
if start < 0 or end < 0:
    raise SystemExit("sticky release section not found in platformio.ini")
text = text[:start] + '; Sticky is development-only in this fork; no tagged release environment is published.\n\n' + text[end:]
p.write_text(text)
