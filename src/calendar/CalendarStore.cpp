#include "calendar/CalendarStore.h"

#include <SDCardManager.h>

#include <string>

#include "calendar/IcsParser.h"

namespace {
constexpr size_t kMaxCacheBytes = 256 * 1024;
}

namespace Calendar {

bool CalendarStore::hasCache() const { return SdMan.exists(kCachePath); }

bool CalendarStore::load(std::vector<Event>& events, int timezoneOffsetMinutes) const {
  FsFile file;
  if (!SdMan.openFileForRead("CAL", kCachePath, file)) return false;

  std::string content;
  content.reserve(8192);
  char buffer[512];
  while (file.available() && content.size() < kMaxCacheBytes) {
    const int read = file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
    if (read <= 0) break;
    const size_t remaining = kMaxCacheBytes - content.size();
    content.append(buffer, static_cast<size_t>(read) > remaining ? remaining : static_cast<size_t>(read));
  }
  const bool overflow = file.available();
  file.close();
  if (overflow || content.empty()) return false;

  return IcsParser::parse(content, events, timezoneOffsetMinutes);
}

bool CalendarStore::save(const std::string& ics) const {
  if (ics.empty() || ics.size() > kMaxCacheBytes || ics.find("BEGIN:VCALENDAR") == std::string::npos) return false;

  SdMan.mkdir("/.calendar");
  constexpr const char* tempPath = "/.calendar/cache.tmp";
  if (SdMan.exists(tempPath)) SdMan.remove(tempPath);

  FsFile file;
  if (!SdMan.openFileForWrite("CAL", tempPath, file)) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(ics.data()), ics.size());
  file.close();
  if (written != ics.size()) {
    SdMan.remove(tempPath);
    return false;
  }

  std::vector<Event> parsed;
  FsFile verify;
  if (!SdMan.openFileForRead("CAL", tempPath, verify)) {
    SdMan.remove(tempPath);
    return false;
  }
  std::string staged;
  staged.reserve(ics.size());
  char buffer[512];
  while (verify.available()) {
    const int read = verify.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
    if (read <= 0) break;
    staged.append(buffer, static_cast<size_t>(read));
  }
  verify.close();
  if (!IcsParser::parse(staged, parsed)) {
    SdMan.remove(tempPath);
    return false;
  }

  if (SdMan.exists(kCachePath)) SdMan.remove(kCachePath);
  if (!SdMan.rename(tempPath, kCachePath)) {
    SdMan.remove(tempPath);
    return false;
  }
  return true;
}

}  // namespace Calendar
