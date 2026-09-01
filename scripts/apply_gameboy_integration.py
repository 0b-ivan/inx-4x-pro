#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/activity/settings/CategorySettingsActivity.cpp",
    '#include "ClockStylePickerActivity.h"\n',
    '#include "ClockStylePickerActivity.h"\n#include "../gameboy/GameBoyActivity.h"\n',
)

replace_once(
    "src/activity/settings/CategorySettingsActivity.cpp",
    '            if (strcmp(settingPtr->name, "About") == 0) {\n',
    '''            if (strcmp(settingPtr->name, "Game Boy") == 0) {\n              exitActivity();\n              enterNewActivity(new GameBoyBrowserActivity(renderer, mappedInput, [this] {\n                exitActivity();\n                updateRequired = true;\n              }));\n              return;\n            }\n            if (strcmp(settingPtr->name, "About") == 0) {\n''',
)

replace_once(
    "src/activity/page/SettingsActivity.cpp",
    '  settings.push_back(SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS));\n',
    '  settings.push_back(SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS));\n'
    '  settings.push_back(SettingInfo::Action("Game Boy", GroupType::DEVICE_ACTIONS));\n',
)

replace_once(
    "src/activity/ActivityWithSubactivity.h",
    '  /** Cleans up the current subactivity on exit. */\n  void onExit() override;\n',
    '''  /** Bubble runtime policy from the active modal/subactivity. */\n  bool skipLoopDelay() override { return subActivity ? subActivity->skipLoopDelay() : Activity::skipLoopDelay(); }\n  bool preventAutoSleep() override {\n    return subActivity ? subActivity->preventAutoSleep() : Activity::preventAutoSleep();\n  }\n  bool allowGlobalPowerRefresh() override {\n    return subActivity ? subActivity->allowGlobalPowerRefresh() : Activity::allowGlobalPowerRefresh();\n  }\n  /** Cleans up the current subactivity on exit. */\n  void onExit() override;\n''',
)

replace_once(
    "src/activity/gameboy/GameBoyActivity.cpp",
    '  const int top = INX_THEME.mainContentTop(renderer) + INX_THEME.mainHeaderHeight();\n',
    '  const int top = INX_THEME.drawerPageHeaderHeight();\n',
)

print("Game Boy integration applied")
