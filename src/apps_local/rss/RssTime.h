#pragma once
#include <cstdint>
#include <ctime>
#include <string>
namespace rsstime {
int offsetSeconds(uint8_t biased);
bool parsePublished(const std::string& value, time_t& epoch);
std::string formatPublished(const std::string& value, uint8_t offset);
int64_t localDay(time_t epoch, uint8_t offset);
int64_t nextSync(time_t now, uint8_t offset, int minute, int64_t lastDay, int64_t lastAttempt);
bool parseSyncTime(const std::string& value, int& minute);
}  // namespace rsstime
