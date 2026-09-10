#include <cassert>
#include <cstdio>

#include "RssTime.h"
int main() {
  time_t a, b;
  assert(rsstime::parsePublished("2026-09-08T23:30:00Z", a));
  assert(rsstime::parsePublished("Wed, 09 Sep 2026 01:30:00 +0200", b));
  assert(a == b);
  assert(rsstime::formatPublished("2026-09-08T23:30:00Z", 56) == "09.09.2026 01:30");
  assert(rsstime::formatPublished("2026-01-01T00:30:00+01:00", 48) == "31.12.2025 23:30");
  assert(rsstime::formatPublished("2026-09-08T23:30:00.123Z", 48) == "08.09.2026 23:30");
  assert(rsstime::formatPublished("Tue, 08 Sep 2026 14:15:00 GMT", 70) == "08.09.2026 19:45");
  assert(rsstime::formatPublished("unknown", 48) == "unknown");
  assert(!rsstime::parsePublished("2026-09-08T12:00:00", a));
  int minute = -1;
  assert(rsstime::parseSyncTime("00:00", minute) && minute == 0);
  assert(rsstime::parseSyncTime("23:59", minute) && minute == 1439);
  for (const char* bad : {"24:00", "06:60", "6:30", "06:30x", "ab:cd", ""})
    assert(!rsstime::parseSyncTime(bad, minute));
  assert(!rsstime::parsePublished("2026-02-29T12:00:00Z", a));
  assert(!rsstime::parsePublished("2026-04-31T12:00:00Z", a));
  assert(rsstime::parsePublished("2024-02-29T12:00:00Z", a));
  assert(rsstime::parsePublished("2026-09-08T04:30:00Z", a));
  const auto day = rsstime::localDay(a, 56);
  assert(rsstime::nextSync(a - 1, 56, 390, -1, 0) == a);
  assert(rsstime::nextSync(a, 56, 390, -1, 0) == a);
  assert(rsstime::nextSync(a + 60, 56, 390, -1, 0) == a);
  assert(rsstime::nextSync(a, 56, 390, day, 0) == a + 86400);
  assert(rsstime::nextSync(a, 56, 390, -1, a) == a + 3600);
  assert(rsstime::nextSync(a + 3600, 56, 390, -1, a) == a + 3600);
  assert(rsstime::nextSync(0, 56, 390, -1, 0) == 0);
  assert(rsstime::nextSync(a, 56, 1440, -1, 0) == 0);
  puts("RSS time and schedule: passed");
}
