#include "calendar/CalendarConfig.h"

#include <ArduinoJson.h>
#include <SDCardManager.h>

#include <algorithm>

namespace {
constexpr const char* kPrimaryConfigPath = "/.calendar/config.json";
constexpr const char* kFallbackConfigPath = "/calendar.json";
constexpr size_t kMaxConfigBytes = 8192;

bool loadPath(const char* path, Calendar::Config& config) {
  if (!SdMan.exists(path)) return false;

  const String raw = SdMan.readFile(path);
  if (raw.isEmpty() || raw.length() > kMaxConfigBytes) return false;

  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) return false;

  config.enabled = doc["enabled"] | false;
  config.calendarUrl = std::string((doc["calendarUrl"] | ""));
  config.username = std::string((doc["username"] | ""));
  config.appPassword = std::string((doc["appPassword"] | ""));
  config.wifiSsid = std::string((doc["wifiSsid"] | ""));

  const int interval = doc["syncIntervalMinutes"] | 180;
  config.syncIntervalMinutes = static_cast<uint16_t>(std::clamp(interval, 30, 1440));
  const int lookAhead = doc["lookAheadDays"] | 7;
  config.lookAheadDays = static_cast<uint8_t>(std::clamp(lookAhead, 1, 14));

  if (!config.enabled) return true;
  return config.calendarUrl.rfind("https://", 0) == 0 && !config.username.empty() && !config.appPassword.empty();
}
}  // namespace

namespace Calendar {

bool ConfigStore::load(Config& config) {
  config = Config{};
  if (loadPath(kPrimaryConfigPath, config)) return true;
  config = Config{};
  return loadPath(kFallbackConfigPath, config);
}

}  // namespace Calendar
