#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

class RssReadingSettings : public PersistableStore<RssReadingSettings> {
  friend class PersistableStore<RssReadingSettings>;
 public:
  bool newestFirst = true;
  bool showRead = false;
  static const char* getFilePath() { return "/.crosspoint/rss-reading.json"; }
  void toJson(JsonDocument& doc) const {
    doc["newestFirst"] = newestFirst;
    doc["showRead"] = showRead;
  }
  bool fromJson(JsonVariantConst doc) {
    newestFirst = doc["newestFirst"] | true;
    showRead = doc["showRead"] | false;
    return true;
  }
};
#define RSS_READING_SETTINGS RssReadingSettings::getInstance()
