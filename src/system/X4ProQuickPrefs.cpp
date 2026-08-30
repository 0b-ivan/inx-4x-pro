#include "X4ProQuickPrefs.h"

#ifndef SIMULATOR
#include <Preferences.h>

namespace {
constexpr char kNamespace[] = "inx-quick";

bool readBool(const char* key, const bool fallback) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return fallback;
  const bool value = prefs.getBool(key, fallback);
  prefs.end();
  return value;
}

void writeBool(const char* key, const bool value) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBool(key, value);
  prefs.end();
}
}  // namespace

bool X4ProQuickPrefs::nightMode() { return readBool("night", false); }

void X4ProQuickPrefs::setNightMode(const bool enabled) { writeBool("night", enabled); }

bool X4ProQuickPrefs::readerTouchEnabled() { return readBool("readerTouch", true); }

void X4ProQuickPrefs::setReaderTouchEnabled(const bool enabled) { writeBool("readerTouch", enabled); }

#else
namespace {
bool gNightMode = false;
bool gReaderTouch = true;
}

bool X4ProQuickPrefs::nightMode() { return gNightMode; }
void X4ProQuickPrefs::setNightMode(const bool enabled) { gNightMode = enabled; }
bool X4ProQuickPrefs::readerTouchEnabled() { return gReaderTouch; }
void X4ProQuickPrefs::setReaderTouchEnabled(const bool enabled) { gReaderTouch = enabled; }
#endif
