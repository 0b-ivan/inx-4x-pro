#pragma once

// Native-only virtual UTC clock for Biscuit's Clock activity. The real ESP32
// builds never include this header and keep Arduino's NTP/getLocalTime path.
#ifdef SIMULATOR
#include <cerrno>
#include <cstdlib>
#include <ctime>

namespace biscuit_sim {
inline bool readClockUtc(std::tm* result) {
  if (!result) return false;
  std::time_t now = std::time(nullptr);
  const char* value = std::getenv("CROSSPLAY_SIM_EPOCH");
  if (value && *value) {
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 0) return false;
    now = static_cast<std::time_t>(parsed);
    if (static_cast<long long>(now) != parsed) return false;
  }
  return gmtime_r(&now, result) != nullptr;
}
}  // namespace biscuit_sim
#endif  // SIMULATOR
