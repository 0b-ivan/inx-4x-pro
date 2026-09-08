#include <WiFi.h>

#include <cassert>
#include <cstdio>
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
  texts[item.guid] = text;
  pending.erase(item.guid);
  return true;
}
bool articlePending(const RssFeed&, const RssItem& item) { return pending.count(item.guid); }
bool markArticlePending(const RssFeed&, const RssItem& item) {
  pending.insert(item.guid);
  return true;
}
bool mergeAndSaveFeed(const RssFeed& feed, const std::string&, const std::vector<RssItem>& fresh,
                      std::vector<RssItem>& merged, RssCacheInfo*) {
  indexes[feed.url] = fresh;
  merged = fresh;
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
  puts("RSS/Atom preload, partial failure and retry: passed");
}
