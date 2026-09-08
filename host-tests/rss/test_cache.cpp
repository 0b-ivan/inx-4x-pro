#include <HalStorage.h>

#include <cassert>
#include <cstdio>

#include "RssCache.h"

int main() {
  RssFeed feed{"Test", "https://test/feed", "", ""};
  RssItem retry;
  retry.guid = "retry";
  retry.link = "https://test/retry";
  std::vector<RssItem> merged;
  assert(rsscache::markArticlePending(feed, retry));
  assert(rsscache::mergeAndSaveFeed(feed, "Test", {retry}, merged));
  std::vector<RssItem> fresh;
  fresh.reserve(rsscache::MAX_CACHED_ITEMS);
  for (size_t i = 0; i < rsscache::MAX_CACHED_ITEMS; ++i) {
    RssItem entry;
    entry.guid = std::to_string(i);
    fresh.push_back(entry);
  }
  assert(rsscache::mergeAndSaveFeed(feed, "Test", fresh, merged));
  assert(merged.size() == rsscache::MAX_CACHED_ITEMS);
  assert(merged.front().guid == "retry");
  // Too many pending entries cannot replace the durable index with a partial one.
  for (const auto& entry : fresh) assert(rsscache::markArticlePending(feed, entry));
  assert(!rsscache::mergeAndSaveFeed(feed, "Overflow", fresh, merged));
  std::string title;
  assert(rsscache::loadFeed(feed, title, merged));
  assert(title == "Test" && merged.front().guid == "retry");
  // Failed article writes preserve the pending marker and are not readable as success.
  teststorage::writable = false;
  assert(!rsscache::saveArticle(feed, retry, "complete text"));
  assert(rsscache::articlePending(feed, retry));
  std::string text;
  assert(!rsscache::loadArticle(feed, retry, text));
  teststorage::writable = true;
  teststorage::writeLimit = 1;
  assert(!rsscache::saveArticle(feed, retry, "complete text"));
  assert(rsscache::articlePending(feed, retry) && !rsscache::loadArticle(feed, retry, text));
  teststorage::writeLimit = 1000000;
  teststorage::renameFails = true;
  assert(!rsscache::saveArticle(feed, retry, "complete text"));
  assert(rsscache::articlePending(feed, retry) && !rsscache::loadArticle(feed, retry, text));
  teststorage::renameFails = false;
  assert(rsscache::saveArticle(feed, retry, "complete text"));
  assert(!rsscache::articlePending(feed, retry));
  assert(rsscache::loadArticle(feed, retry, text) && text == "complete text");
  puts("RSS cache retry retention, capacity rejection and article persistence: passed");
}
