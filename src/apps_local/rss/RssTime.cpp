#include "RssTime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace rsstime {
int offsetSeconds(uint8_t biased) { return (std::min<int>(biased, 104) - 48) * 900; }
int64_t localDay(time_t epoch, uint8_t offset) { return (static_cast<int64_t>(epoch) + offsetSeconds(offset)) / 86400; }
int64_t nextSync(time_t now, uint8_t offset, int minute, int64_t lastDay, int64_t lastAttempt) {
  if (now < 1609459200 || minute < 0 || minute >= 1440) return 0;
  const int64_t day = localDay(now, offset);
  int64_t next = day * 86400 + minute * 60 - offsetSeconds(offset);
  if (lastDay >= day) next = (lastDay + 1) * 86400 + minute * 60 - offsetSeconds(offset);
  if (lastAttempt <= now) next = std::max(next, lastAttempt + 3600);
  return next;
}
bool parseSyncTime(const std::string& value, int& minute) {
  if (value.size() != 5 || value[2] != ':') return false;
  for (int i : {0, 1, 3, 4})
    if (value[i] < '0' || value[i] > '9') return false;
  const int h = (value[0] - '0') * 10 + value[1] - '0';
  const int m = (value[3] - '0') * 10 + value[4] - '0';
  if (h > 23 || m > 59) return false;
  minute = h * 60 + m;
  return true;
}
bool parsePublished(const std::string& value, time_t& epoch) {
  tm t{};
  const char* tail = strptime(value.c_str(), "%Y-%m-%dT%H:%M:%S", &t);
  if (!tail) tail = strptime(value.c_str(), "%Y-%m-%dT%H:%M", &t);
  if (!tail) {
    const char* start = value.c_str();
    if (const char* comma = strchr(start, ',')) start = comma + 1;
    while (*start == ' ') ++start;
    tail = strptime(start, "%d %b %Y %H:%M:%S", &t);
    if (!tail) tail = strptime(start, "%d %b %Y %H:%M", &t);
  }
  if (!tail) return false;
  if (*tail == '.') {
    ++tail;
    while (*tail >= '0' && *tail <= '9') ++tail;
  }
  while (*tail == ' ') ++tail;
  int zone = 0;
  if (*tail == '+' || *tail == '-') {
    const int sign = *tail++ == '-' ? -1 : 1;
    int h = 0, m = 0, consumed = 0;
    if (sscanf(tail, "%2d:%2d%n", &h, &m, &consumed) != 2) {
      consumed = 0;
      if (sscanf(tail, "%2d%2d%n", &h, &m, &consumed) != 2) return false;
    }
    if (h > 23 || m > 59 || tail[consumed]) return false;
    zone = sign * (h * 3600 + m * 60);
  } else if (strcmp(tail, "Z") && strcmp(tail, "GMT") && strcmp(tail, "UTC") && strcmp(tail, "UT")) {
    return false;
  }
  // Civil date to Unix days, independent of the host/device TZ environment.
  int year = t.tm_year + 1900;
  const int month = t.tm_mon + 1;
  if (year < 1970 || month < 1 || month > 12 || t.tm_mday < 1 || t.tm_mday > 31) return false;
  static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (t.tm_mday > days[month - 1] + (month == 2 && leap)) return false;
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yoe = year - era * 400;
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  epoch = static_cast<time_t>((static_cast<int64_t>(era) * 146097 + doe - 719468) * 86400 + t.tm_hour * 3600 +
                              t.tm_min * 60 + t.tm_sec - zone);
  return true;
}
std::string formatPublished(const std::string& value, uint8_t offset) {
  time_t epoch;
  if (!parsePublished(value, epoch)) return value;
  epoch += offsetSeconds(offset);
  tm local{};
  if (!gmtime_r(&epoch, &local)) return value;
  char out[32];
  strftime(out, sizeof(out), "%d.%m.%Y %H:%M", &local);
  return out;
}
}  // namespace rsstime
