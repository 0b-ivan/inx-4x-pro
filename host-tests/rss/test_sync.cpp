#include <WiFi.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>

#include "RssCache.h"
#include "RssSync.h"
#include "RssSyncSettings.h"
#include "network/HttpDownloader.h"
std::map<std::string, std::vector<RssItem>> indexes;
std::map<std::string, std::string> texts;
std::set<std::string> pending, failures;
std::vector<std::string> downloads;
bool cacheWritable = true;
namespace rsssync {
std::string fetchArticleText(const RssFeed&, const RssItem& item) {
  downloads.push_back(item.link);
  return failures.count(item.link) ? "" : "Complete article " + item.link;
}
}  // namespace rsssync
namespace rsscache {
bool sameItem(const RssItem& a, const RssItem& b) { return a.guid == b.guid; }
bool loadFeed(const RssFeed& feed, std::string&, std::vector<RssItem>& items, RssCacheInfo*) {
  items = indexes[feed.url];
  return !items.empty();
}
bool loadArticle(const RssFeed&, const RssItem& item, std::string& text) {
  text = texts[item.guid];
  return !text.empty();
}
bool saveArticle(const RssFeed&, const RssItem& item, const std::string& text) {
  if (!cacheWritable) return false;
  texts[item.guid] = text;
  pending.erase(item.guid);
  return true;
}
bool articlePending(const RssFeed&, const RssItem& item) { return pending.count(item.guid); }
bool markArticlePending(const RssFeed&, const RssItem& item) {
  if (pending.count(item.guid)) return true;
  if (!cacheWritable) return false;
  pending.insert(item.guid);
  return true;
}
bool mergeAndSaveFeed(const RssFeed& feed, const std::string&, const std::vector<RssItem>& fresh,
                      std::vector<RssItem>& merged, RssCacheInfo*) {
  if (!cacheWritable) return false;
  auto previous = indexes[feed.url];
  indexes[feed.url] = fresh;
  for (const auto& old : previous) {
    if (std::none_of(fresh.begin(), fresh.end(), [&](const RssItem& entry) { return sameItem(entry, old); }))
      indexes[feed.url].push_back(old);
  }
  merged = indexes[feed.url];
  return true;
}
}  // namespace rsscache
std::string item(const char* id, const std::string& date) {
  return "<item><guid>" + std::string(id) + "</guid><title>" + id + "</title><link>https://test/" + id +
         "</link><pubDate>" + date + "</pubDate><description>Summary</description></item>";
}
int main() {
  char today[32];
  time_t now = time(nullptr);
  tm t{};
  gmtime_r(&now, &t);
  strftime(today, sizeof(today), "%Y-%m-%dT%H:%M:%SZ", &t);
  RssItem old;
  old.guid = "old";
  RssItem current;
  current.guid = "today";
  RssItem cached;
  cached.guid = "cached";
  RssFeed rss{"RSS", "rss", "", ""}, atom{"Atom", "atom", "", ""};
  indexes["rss"] = {old, current, cached};
  texts["cached"] = "Already cached";
  responses["rss"] = "<rss><channel><title>RSS</title>" + item("old", "2020-01-01T00:00:00Z") + item("today", today) +
                     item("new", "2020-01-01T00:00:00Z") + item("cached", today) + "</channel></rss>";
  responses["atom"] =
      "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>Atom</title><entry><id>atom-item</id>"
      "<title>Atom item</title><link rel=\"self\" href=\"https://test/feed-entry\"/><link "
      "href=\"https://test/atom\"/><updated>2020-01-01T00:00:00Z</updated></entry></feed>";
  RSS_STORE.feeds = {rss, atom};
  failures.insert("https://test/new");
  assert(!rsssync::syncAll());
  assert(downloads.size() == 3);  // today + new + second feed, despite the first failure
  assert(texts["old"].empty());
  assert(texts["today"] == "Complete article https://test/today");
  assert(texts["atom-item"] == "Complete article https://test/atom");
  assert(pending.count("new") && indexes["rss"].size() == 4);
  assert(WiFi.modeValue == WIFI_OFF);
  failures.clear();
  downloads.clear();
  assert(rsssync::syncAll());
  assert(downloads.size() == 1 && downloads[0] == "https://test/new");
  assert(pending.empty());
  downloads.clear();
  assert(rsssync::syncAll() && downloads.empty());
  responses["rss"] = "<rss>invalid";
  assert(!rsssync::syncAll());
  assert(indexes["rss"].size() == 4);
  // Each call performs at most one article operation; start does no network I/O.
  indexes.clear();
  texts.clear();
  pending.clear();
  downloads.clear();
  responses["rss"] =
      "<rss><channel><title>RSS</title>" + item("first", today) + item("second", today) + "</channel></rss>";
  RSS_STORE.feeds = {rss};
  assert(rsssync::start());
  assert(!rsssync::start() && !rsssync::syncAll());
  assert(downloads.empty());
  rsssync::step();  // Wi-Fi
  rsssync::step();  // feed only, both retry markers durable
  assert(downloads.empty() && pending.size() == 2);
  assert(rsssync::progress().articlesTotal == 2);
  rsssync::step();
  assert(downloads.size() == 1 && rsssync::progress().articlesSaved == 1);
  rsssync::cancel();
  assert(!rsssync::active() && rsssync::progress().state == rsssync::SyncState::Cancelled);
  assert(pending.count("second") && !pending.count("first"));
  assert(WiFi.modeValue == WIFI_OFF);
  rsssync::step();
  assert(downloads.size() == 1);
  // Retry an item even after it disappears from the feed and its date is old.
  indexes["rss"][1].published = "2020-01-01T00:00:00Z";
  responses["rss"] = "<rss><channel><title>RSS</title></channel></rss>";
  assert(rsssync::syncAll());
  assert(downloads.size() == 2 && pending.empty());

  // A failed feed cannot block an independent valid feed.
  responses["rss"] = "invalid";
  indexes.erase("atom");
  texts.erase("atom-item");
  RSS_STORE.feeds = {rss, atom};
  assert(!rsssync::syncAll());
  assert(rsssync::progress().feedsDone == 2 && rsssync::progress().feedsFailed == 1);
  assert(rsssync::progress().failedFeedMask == 1 && !texts["atom-item"].empty());

  // SD failure at preparation and during article save never reports success.
  RSS_STORE.feeds = {atom};
  cacheWritable = false;
  assert(!rsssync::syncAll());
  cacheWritable = true;
  texts.clear();
  pending.insert("atom-item");
  assert(rsssync::start());
  rsssync::step();
  rsssync::step();
  cacheWritable = false;
  while (rsssync::active()) rsssync::step();
  assert(rsssync::progress().state == rsssync::SyncState::Incomplete);
  assert(rsssync::progress().articlesSaved == 0 && rsssync::progress().articlesFailed == 1);
  assert(pending.count("atom-item"));
  cacheWritable = true;
  assert(rsssync::syncAll());

  WiFi.succeed = false;
  assert(rsssync::start());
  rsssync::step();
  assert(!rsssync::active() && rsssync::progress().state == rsssync::SyncState::Incomplete);
  assert(WiFi.modeValue == WIFI_OFF);
  WiFi.succeed = true;
  WiFi.modeValue = WIFI_STA;
  WiFi.statusValue = WL_CONNECTED;
  assert(rsssync::start());
  rsssync::step();
  rsssync::cancel();
  assert(WiFi.modeValue == WIFI_STA && WiFi.statusValue == WL_CONNECTED);

  // Timer wake drains the same job and records the day only after complete storage.
  auto& schedule = RSS_SYNC_SETTINGS;
  schedule.enabled = true;
  schedule.minute = 0;
  schedule.lastDay = -1;
  schedule.lastAttempt = 0;
  texts.clear();
  pending.insert("atom-item");
  failures.insert("https://test/atom");
  rsssync::tick(true);
  assert(!rsssync::active() && schedule.lastDay == -1 && pending.count("atom-item"));
  failures.clear();
  schedule.lastAttempt = 0;
  rsssync::tick(true);
  assert(!rsssync::active() && schedule.lastDay >= 0 && pending.empty());
  // Regular scheduling yields to the main loop before network work.
  schedule.lastDay = -1;
  schedule.lastAttempt = 0;
  clockMs += 60000;
  rsssync::tick();
  assert(rsssync::active() && rsssync::progress().state == rsssync::SyncState::Connecting);
  rsssync::cancel();
  assert(schedule.lastDay == -1);
  schedule.lastAttempt = 0;
  testSavesUntilFailure = 0;
  rsssync::tick(true);
  assert(!rsssync::active() && schedule.lastAttempt == 0 && schedule.lastDay == -1);
  testSavesUntilFailure = 1;
  rsssync::tick(true);
  assert(!rsssync::active() && schedule.lastDay == -1);
  assert(rsssync::progress().state == rsssync::SyncState::Incomplete);
  testSavesUntilFailure = -1;
  RSS_STORE.feeds.clear();
  assert(!rsssync::start());
  puts("RSS steps, progress, cancellation, retry, SD errors, feed isolation and timer scheduling: passed");
}
