#pragma once

#include <cstdint>
#include <string>

namespace Calendar {

struct DateTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  bool allDay = false;
};

struct Event {
  std::string uid;
  std::string summary;
  std::string location;
  DateTime start;
  DateTime end;
  bool cancelled = false;
};

inline int dateKey(const DateTime& value) {
  return static_cast<int>(value.year) * 10000 + static_cast<int>(value.month) * 100 + static_cast<int>(value.day);
}

inline int timeKey(const DateTime& value) {
  return dateKey(value) * 10000 + static_cast<int>(value.hour) * 100 + static_cast<int>(value.minute);
}

}  // namespace Calendar
