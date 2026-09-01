#pragma once

#include <string>
#include <vector>

#include "calendar/CalendarTypes.h"

namespace Calendar {

class CalendarStore {
 public:
  static constexpr const char* kCachePath = "/.calendar/cache.ics";

  bool load(std::vector<Event>& events, int timezoneOffsetMinutes = 0) const;
  bool save(const std::string& ics) const;
  bool hasCache() const;
};

}  // namespace Calendar
