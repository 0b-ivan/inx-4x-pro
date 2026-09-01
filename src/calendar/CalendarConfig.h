#pragma once

#include <cstdint>
#include <string>

namespace Calendar {

struct Config {
  bool enabled = false;
  std::string calendarUrl;
  std::string username;
  std::string appPassword;
  std::string wifiSsid;
  uint16_t syncIntervalMinutes = 180;
  uint8_t lookAheadDays = 7;
};

class ConfigStore {
 public:
  static bool load(Config& config);
};

}  // namespace Calendar
