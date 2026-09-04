#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

struct RssFeed {
  std::string name;
  std::string url;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
};

class RssFeedStore : public PersistableStore<RssFeedStore> {
 private:
  std::vector<RssFeed> feeds;

  static constexpr size_t MAX_FEEDS = 8;

  RssFeedStore() = default;

  friend class PersistableStore<RssFeedStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/rss.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addFeed(const RssFeed& feed);
  bool updateFeed(size_t index, const RssFeed& feed);
  bool removeFeed(size_t index);

  const std::vector<RssFeed>& getFeeds() const { return feeds; }
  const RssFeed* getFeed(size_t index) const;
  size_t getCount() const { return feeds.size(); }
  bool hasFeeds() const { return !feeds.empty(); }
};

#define RSS_STORE RssFeedStore::getInstance()
