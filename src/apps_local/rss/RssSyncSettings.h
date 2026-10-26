#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
class RssSyncSettings : public PersistableStore<RssSyncSettings> {
  friend class PersistableStore<RssSyncSettings>;

 public:
  bool enabled = false;
  int minute = 390;
  int64_t lastDay = -1;
  int64_t lastAttempt = 0;
  static const char* getFilePath() { return "/.crosspoint/rss-sync.json"; }
  void toJson(JsonDocument& doc) const {
    doc["enabled"] = enabled;
    doc["minute"] = minute;
    doc["lastDay"] = lastDay;
    doc["lastAttempt"] = lastAttempt;
  }
  bool fromJson(JsonVariantConst doc) {
    enabled = doc["enabled"] | false;
    minute = doc["minute"] | 390;
    if (minute < 0 || minute >= 1440) {
      minute = 390;
      enabled = false;
    }
    lastDay = doc["lastDay"] | int64_t(-1);
    lastAttempt = doc["lastAttempt"] | int64_t(0);
    return true;
  }
};
#define RSS_SYNC_SETTINGS RssSyncSettings::getInstance()
