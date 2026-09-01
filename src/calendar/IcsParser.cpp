#include "calendar/IcsParser.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

bool digits(const std::string& s, size_t pos, size_t count, int& out) {
  if (pos + count > s.size()) return false;
  int value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char c = s[pos + i];
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

bool leapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(int year, int month) {
  static constexpr int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  if (month == 2 && leapYear(year)) return 29;
  return days[month];
}

void addMinutes(Calendar::DateTime& dt, int minutes) {
  int total = static_cast<int>(dt.hour) * 60 + static_cast<int>(dt.minute) + minutes;
  while (total < 0) {
    total += 24 * 60;
    if (--dt.day == 0) {
      if (--dt.month == 0) {
        dt.month = 12;
        --dt.year;
      }
      dt.day = static_cast<uint8_t>(daysInMonth(dt.year, dt.month));
    }
  }
  while (total >= 24 * 60) {
    total -= 24 * 60;
    const int dim = daysInMonth(dt.year, dt.month);
    if (++dt.day > dim) {
      dt.day = 1;
      if (++dt.month > 12) {
        dt.month = 1;
        ++dt.year;
      }
    }
  }
  dt.hour = static_cast<uint8_t>(total / 60);
  dt.minute = static_cast<uint8_t>(total % 60);
}

std::string unescapeText(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      const char next = input[++i];
      if (next == 'n' || next == 'N') out.push_back(' ');
      else out.push_back(next);
    } else {
      out.push_back(input[i]);
    }
  }
  return out;
}

std::vector<std::string> unfoldLines(const std::string& input) {
  std::vector<std::string> lines;
  std::string current;
  size_t start = 0;
  while (start <= input.size()) {
    const size_t end = input.find('\n', start);
    std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && (line.front() == ' ' || line.front() == '\t') && !current.empty()) {
      current += line.substr(1);
    } else {
      if (!current.empty()) lines.push_back(current);
      current = std::move(line);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}

bool parseDateTime(const std::string& property, const std::string& value, int timezoneOffsetMinutes,
                   Calendar::DateTime& out) {
  int year = 0, month = 0, day = 0;
  if (!digits(value, 0, 4, year) || !digits(value, 4, 2, month) || !digits(value, 6, 2, day)) return false;
  if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) return false;

  out.year = static_cast<uint16_t>(year);
  out.month = static_cast<uint8_t>(month);
  out.day = static_cast<uint8_t>(day);
  out.hour = 0;
  out.minute = 0;
  out.allDay = property.find("VALUE=DATE") != std::string::npos || value.size() == 8;
  if (out.allDay) return true;

  if (value.size() < 13 || value[8] != 'T') return false;
  int hour = 0, minute = 0;
  if (!digits(value, 9, 2, hour) || !digits(value, 11, 2, minute)) return false;
  if (hour > 23 || minute > 59) return false;
  out.hour = static_cast<uint8_t>(hour);
  out.minute = static_cast<uint8_t>(minute);

  if (!value.empty() && value.back() == 'Z') addMinutes(out, timezoneOffsetMinutes);
  return true;
}

}  // namespace

namespace Calendar {

bool IcsParser::parse(const std::string& ics, std::vector<Event>& events, int timezoneOffsetMinutes, size_t maxEvents) {
  events.clear();
  const auto lines = unfoldLines(ics);
  Event current;
  bool inEvent = false;
  bool haveStart = false;

  for (const auto& line : lines) {
    if (line == "BEGIN:VEVENT") {
      current = Event{};
      inEvent = true;
      haveStart = false;
      continue;
    }
    if (line == "END:VEVENT") {
      if (inEvent && haveStart && !current.cancelled) {
        events.push_back(current);
        if (events.size() >= maxEvents) break;
      }
      inEvent = false;
      continue;
    }
    if (!inEvent) continue;

    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string property = line.substr(0, colon);
    const std::string value = line.substr(colon + 1);
    const size_t semicolon = property.find(';');
    const std::string name = property.substr(0, semicolon);

    if (name == "UID") current.uid = value;
    else if (name == "SUMMARY") current.summary = unescapeText(value);
    else if (name == "LOCATION") current.location = unescapeText(value);
    else if (name == "STATUS" && value == "CANCELLED") current.cancelled = true;
    else if (name == "DTSTART") haveStart = parseDateTime(property, value, timezoneOffsetMinutes, current.start);
    else if (name == "DTEND") parseDateTime(property, value, timezoneOffsetMinutes, current.end);
  }

  std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    if (dateKey(a.start) != dateKey(b.start)) return dateKey(a.start) < dateKey(b.start);
    if (a.start.allDay != b.start.allDay) return a.start.allDay;
    return timeKey(a.start) < timeKey(b.start);
  });
  return true;
}

}  // namespace Calendar
