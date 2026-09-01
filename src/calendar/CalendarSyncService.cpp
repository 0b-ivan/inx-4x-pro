#include "calendar/CalendarSyncService.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "calendar/CalendarConfig.h"
#include "network/CalDavClient.h"
#include "state/SystemSetting.h"

#ifndef SIMULATOR
#include <WiFi.h>
#include "state/NetworkCredential.h"
#endif

namespace {
constexpr const char* kLastSyncPath = "/.calendar/last_sync.txt";

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const int adjustedMonth = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned doy = (153U * static_cast<unsigned>(adjustedMonth) + 2U) / 5U + day - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
  year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
  const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
  const unsigned mp = (5U * doy + 2U) / 153U;
  day = doy - (153U * mp + 2U) / 5U + 1U;
  const int calculatedMonth = static_cast<int>(mp) + (mp < 10U ? 3 : -9);
  month = static_cast<unsigned>(calculatedMonth);
  year += month <= 2U;
}

int64_t minuteStamp(const SleepClockRenderer::DateTimeView& now) {
  return daysFromCivil(now.year, now.month, now.day) * 1440 + static_cast<int64_t>(now.hour) * 60 + now.minute;
}

bool isDue(const SleepClockRenderer::DateTimeView& now, uint16_t intervalMinutes) {
  if (!SdMan.exists(kLastSyncPath)) return true;
  const String raw = SdMan.readFile(kLastSyncPath);
  if (raw.length() == 0) return true;
  char* end = nullptr;
  const int64_t previous = std::strtoll(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return true;
  const int64_t current = minuteStamp(now);
  if (current < previous) return true;
  return current - previous >= intervalMinutes;
}

void recordSync(const SleepClockRenderer::DateTimeView& now) {
  SdMan.mkdir("/.calendar");
  const std::string stamp = std::to_string(minuteStamp(now));
  SdMan.writeFile(kLastSyncPath, String(stamp.c_str()));
}

std::string formatUtcRange(int64_t utcMinutes) {
  int64_t dayStamp = utcMinutes / 1440;
  int minuteOfDay = static_cast<int>(utcMinutes % 1440);
  if (minuteOfDay < 0) {
    minuteOfDay += 1440;
    --dayStamp;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(dayStamp, year, month, day);

  char out[64];
  std::snprintf(out, sizeof(out), "%04d%02u%02uT%02d%02d00Z", year, month, day, minuteOfDay / 60,
                minuteOfDay % 60);
  return out;
}

void queryWindow(const SleepClockRenderer::DateTimeView& now, uint8_t lookAheadDays, std::string& startUtc,
                 std::string& endUtc) {
  const int timezoneOffset = SETTINGS.getTimeZoneOffsetMinutes();
  const int64_t localMidnight = daysFromCivil(now.year, now.month, now.day) * 1440;
  const int64_t utcStart = localMidnight - timezoneOffset;
  const int64_t utcEnd = utcStart + static_cast<int64_t>(lookAheadDays + 1) * 1440;
  startUtc = formatUtcRange(utcStart);
  endUtc = formatUtcRange(utcEnd);
}

#ifndef SIMULATOR
bool connectSavedWifi(const Calendar::Config& config, bool& startedWifi) {
  startedWifi = false;
  if (WiFi.status() == WL_CONNECTED) return true;

  WIFI_STORE.loadFromFile();
  const auto& credentials = WIFI_STORE.getCredentials();
  if (credentials.empty()) return false;

  const WifiCredential* selected = nullptr;
  if (!config.wifiSsid.empty()) {
    selected = WIFI_STORE.findCredential(config.wifiSsid);
  } else {
    selected = &credentials.front();
  }
  if (!selected) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(selected->ssid.c_str(), selected->password.c_str());
  startedWifi = true;

  const uint32_t deadline = millis() + 10000;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(deadline - millis()) > 0) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  startedWifi = false;
  return false;
}

void stopWifiIfOwned(bool startedWifi) {
  if (!startedWifi) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
#endif
}  // namespace

namespace CalendarSyncService {

bool syncIfDue(const SleepClockRenderer::DateTimeView& now) {
  Calendar::Config config;
  if (!Calendar::ConfigStore::load(config) || !config.enabled || !isDue(now, config.syncIntervalMinutes)) return false;

#ifdef SIMULATOR
  (void)now;
  return false;
#else
  bool startedWifi = false;
  if (!connectSavedWifi(config, startedWifi)) {
    Serial.printf("[%lu] [CALDAV] No saved WiFi available for calendar sync\n", millis());
    return false;
  }

  std::string rangeStart;
  std::string rangeEnd;
  queryWindow(now, config.lookAheadDays, rangeStart, rangeEnd);

  CalDav::Config caldav{config.calendarUrl, config.username, config.appPassword};
  const bool ok = CalDav::Client::sync(caldav, rangeStart, rangeEnd);
  if (ok) recordSync(now);
  stopWifiIfOwned(startedWifi);
  return ok;
#endif
}

uint32_t nextWakeSeconds(const SleepClockRenderer::DateTimeView& now) {
  Calendar::Config config;
  if (!Calendar::ConfigStore::load(config) || !config.enabled) return 0;

  const uint32_t syncSeconds = static_cast<uint32_t>(config.syncIntervalMinutes) * 60U;
  const uint32_t secondsToMidnight =
      static_cast<uint32_t>(23U - std::min<uint8_t>(now.hour, 23U)) * 3600U +
      static_cast<uint32_t>(59U - std::min<uint8_t>(now.minute, 59U)) * 60U + 60U;
  return std::max<uint32_t>(60U, std::min(syncSeconds, secondsToMidnight));
}

}  // namespace CalendarSyncService
