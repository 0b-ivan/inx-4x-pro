#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        if new in text:
            return
        raise SystemExit(f"anchor not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


# Settings currently names this panel "System settings". Match the stable
# prefix rather than relying on a display-title literal.
replace_once(
    "src/activity/settings/CategorySettingsActivity.cpp",
    'categoryName != nullptr && strcmp(categoryName, "System") == 0 && isGroupExpanded(GroupType::DEVICE_ACTIONS)',
    'categoryName != nullptr && strncmp(categoryName, "System", 6) == 0 && isGroupExpanded(GroupType::DEVICE_ACTIONS)',
)

# The ROM rows start immediately after drawPageHeader(), i.e. at the drawer
# page-header height. Keep touch hit-testing on exactly the same geometry.
replace_once(
    "src/activity/gameboy/GameBoyActivity.cpp",
    '  const int top = INX_THEME.mainContentTop(renderer) + INX_THEME.mainHeaderHeight();\n',
    '  const int top = INX_THEME.drawerPageHeaderHeight();\n',
)

# Remove the temporary compatibility overload that was only needed by the
# incorrect Game Boy touch expression above.
replace_once(
    "src/system/UiTheme.h",
    '  int mainContentTop(const GfxRenderer&) const { return mainContentTop(); }\n',
    '',
)

print("Final Game Boy integration fixes applied")
