#include "system/DeskCalendarRenderer.h"

#include <GfxRenderer.h>
#ifndef SIMULATOR
#include <esp_sleep.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "calendar/CalendarStore.h"
#include "calendar/CalendarSyncService.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace {

constexpr const char* WEEKDAYS[] = {"", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};
constexpr const char* MONTHS[] = {"", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

bool leapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(int year, int month) {
  static constexpr int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && leapYear(year)) return 29;
  return month >= 1 && month <= 12 ? days[month] : 30;
}

int tomorrowKey(const SleepClockRenderer::DateTimeView& now) {
  int year = now.year;
  int month = now.month;
  int day = now.day + 1;
  if (day > daysInMonth(year, month)) {
    day = 1;
    if (++month > 12) {
      month = 1;
      ++year;
    }
  }
  return year * 10000 + month * 100 + day;
}

std::string clip(GfxRenderer& renderer, int font, std::string text, int maxWidth,
                 EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (maxWidth <= 0) return "";
  if (renderer.text.getWidth(font, text.c_str(), style) <= maxWidth) return text;
  constexpr const char* suffix = "...";
  while (!text.empty()) {
    text.pop_back();
    const std::string candidate = text + suffix;
    if (renderer.text.getWidth(font, candidate.c_str(), style) <= maxWidth) return candidate;
  }
  return "";
}

bool isVisibleEvent(const Calendar::Event& event, int today, int nowTime) {
  const int date = Calendar::dateKey(event.start);
  if (date < today || event.cancelled) return false;
  if (date > today || event.start.allDay) return true;
  const int eventTime = static_cast<int>(event.start.hour) * 100 + event.start.minute;
  return eventTime >= nowTime;
}

void renderHeader(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& now, int x, int y, int w) {
  const char* weekday = now.weekday <= 7 ? WEEKDAYS[now.weekday] : "";
  renderer.text.render(ATKINSON_HYPERLEGIBLE_16_FONT_ID, x, y, weekday, true, EpdFontFamily::BOLD);

  char date[24];
  std::snprintf(date, sizeof(date), "%02u %s %04u", now.day, now.month <= 12 ? MONTHS[now.month] : "", now.year);
  const int dateW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_14_FONT_ID, date, EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_14_FONT_ID, std::max(x, x + w - dateW), y + 2, date, true,
                       EpdFontFamily::BOLD);
}

void armNextCalendarWake(const SleepClockRenderer::DateTimeView& dateTime) {
#ifndef SIMULATOR
  const uint32_t seconds = CalendarSyncService::nextWakeSeconds(dateTime);
  if (seconds > 0) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  }
#else
  (void)dateTime;
#endif
}

}  // namespace

namespace DeskCalendarRenderer {

bool render(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dateTime, int x, int y, int w, int h) {
  if (w < 300 || h < 260) return false;

  // This is a no-op unless calendar.json exists, the feature is enabled and
  // the configured interval elapsed. Failures never invalidate the old cache.
  CalendarSyncService::syncIfDue(dateTime);
  armNextCalendarWake(dateTime);

  Calendar::CalendarStore store;
  std::vector<Calendar::Event> events;
  if (!store.load(events, SETTINGS.getTimeZoneOffsetMinutes())) return false;

  const int today = static_cast<int>(dateTime.year) * 10000 + static_cast<int>(dateTime.month) * 100 + dateTime.day;
  const int tomorrow = tomorrowKey(dateTime);
  const int nowTime = static_cast<int>(dateTime.hour) * 100 + dateTime.minute;
  events.erase(std::remove_if(events.begin(), events.end(), [today, nowTime](const Calendar::Event& event) {
                 return !isVisibleEvent(event, today, nowTime);
               }),
               events.end());

  renderer.clearScreen(0xff);
  const int margin = std::max(18, w / 30);
  const int left = x + margin;
  const int right = x + w - margin;
  int cursorY = y + 18;

  renderHeader(renderer, dateTime, left, cursorY, right - left);
  cursorY += 42;
  renderer.line.render(left, cursorY, right, cursorY, true);
  cursorY += 16;

  char time[8];
  std::snprintf(time, sizeof(time), "%02u:%02u", dateTime.hour, dateTime.minute);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_18_FONT_ID, left, cursorY, time, true, EpdFontFamily::BOLD);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left + 92, cursorY + 7, "UPCOMING", true,
                       EpdFontFamily::BOLD);
  cursorY += 45;

  if (events.empty()) {
    renderer.text.render(ATKINSON_HYPERLEGIBLE_16_FONT_ID, left, cursorY + 18, "No more appointments", true,
                         EpdFontFamily::BOLD);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left, cursorY + 58,
                         "Calendar cache is up to date or contains no upcoming events.", true);
    return true;
  }

  int lastDate = -1;
  int rows = 0;
  const int footerY = y + h - 34;
  for (const auto& event : events) {
    if (rows >= 7 || cursorY + 54 >= footerY) break;
    const int eventDate = Calendar::dateKey(event.start);
    if (eventDate != lastDate) {
      if (lastDate != -1) cursorY += 6;
      std::string group;
      if (eventDate == today) group = "TODAY";
      else if (eventDate == tomorrow) group = "TOMORROW";
      else {
        char dateLabel[16];
        std::snprintf(dateLabel, sizeof(dateLabel), "%02u %s", event.start.day,
                      event.start.month <= 12 ? MONTHS[event.start.month] : "");
        group = dateLabel;
      }
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, left, cursorY, group.c_str(), true,
                           EpdFontFamily::BOLD);
      cursorY += 26;
      lastDate = eventDate;
    }

    char eventTime[8];
    if (event.start.allDay) std::snprintf(eventTime, sizeof(eventTime), "ALL");
    else std::snprintf(eventTime, sizeof(eventTime), "%02u:%02u", event.start.hour, event.start.minute);

    constexpr int timeWidth = 74;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, left, cursorY, eventTime, true, EpdFontFamily::BOLD);
    const int textX = left + timeWidth;
    const std::string title = clip(renderer, ATKINSON_HYPERLEGIBLE_12_FONT_ID,
                                   event.summary.empty() ? std::string("(untitled)") : event.summary, right - textX,
                                   EpdFontFamily::BOLD);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, cursorY, title.c_str(), true, EpdFontFamily::BOLD);

    if (!event.location.empty() && cursorY + 24 < footerY) {
      const std::string location = clip(renderer, ATKINSON_HYPERLEGIBLE_8_FONT_ID, event.location, right - textX);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, textX, cursorY + 25, location.c_str(), true);
      cursorY += 48;
    } else {
      cursorY += 38;
    }
    ++rows;
  }

  renderer.line.render(left, footerY - 10, right, footerY - 10, true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, left, footerY, "CalDAV cache", true);
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, right - 94, footerY, "read-only MVP", true);
  return true;
}

}  // namespace DeskCalendarRenderer
