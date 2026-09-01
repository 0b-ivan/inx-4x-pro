#pragma once

#include <string>

namespace CalDav {

struct Config {
  std::string calendarUrl;
  std::string username;
  std::string password;
};

class Client {
 public:
  // UTC range must use CalDAV basic date-time form, e.g. 20260901T000000Z.
  // On success, the expanded event set replaces the last-good SD cache.
  static bool sync(const Config& config, const std::string& rangeStartUtc, const std::string& rangeEndUtc);
};

}  // namespace CalDav
