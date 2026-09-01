#pragma once

#include <string>
#include <vector>

#include "calendar/CalendarTypes.h"

namespace Calendar {

class IcsParser {
 public:
  static bool parse(const std::string& ics, std::vector<Event>& events, int timezoneOffsetMinutes = 0,
                    size_t maxEvents = 128);
};

}  // namespace Calendar
