#include "RssFeedStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

void RssFeedStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["feeds"].to<JsonArray>();
  for (const auto& feed : feeds) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = feed.name;
    obj["url"] = feed.url;
    obj["username"] = feed.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(feed.password);
  }
}

bool RssFeedStore::fromJson(JsonVariantConst doc) {
  feeds.clear();
  JsonArrayConst arr = doc["feeds"].as<JsonArrayConst>();
  feeds.reserve(std::min(arr.size(), MAX_FEEDS));
  bool needsResave = false;

  for (JsonObjectConst obj : arr) {
    if (feeds.size() >= MAX_FEEDS) break;
    RssFeed feed;
    feed.name = obj["name"] | "";
    feed.url = obj["url"] | "";
    feed.username = obj["username"] | "";
    feed.password = extractPassword(obj, needsResave);
    feeds.push_back(std::move(feed));
  }

  LOG_DBG("RSS", "Loaded %zu RSS feeds from file", feeds.size());

  if (needsResave) {
    LOG_DBG("RSS", "Resaving JSON with obfuscated passwords");
    requestResave();
  }

  return true;
}

bool RssFeedStore::addFeed(const RssFeed& feed) {
  if (feeds.size() >= MAX_FEEDS) {
    LOG_DBG("RSS", "Cannot add more feeds, limit of %zu reached", MAX_FEEDS);
    return false;
  }
  feeds.push_back(feed);
  return saveToFile();
}

bool RssFeedStore::updateFeed(size_t index, const RssFeed& feed) {
  if (index >= feeds.size()) return false;
  feeds[index] = feed;
  return saveToFile();
}

bool RssFeedStore::removeFeed(size_t index) {
  if (index >= feeds.size()) return false;
  feeds.erase(feeds.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const RssFeed* RssFeedStore::getFeed(size_t index) const {
  if (index >= feeds.size()) return nullptr;
  return &feeds[index];
}
