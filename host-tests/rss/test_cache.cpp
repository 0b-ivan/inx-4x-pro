#include <HalStorage.h>

#include <cassert>
#include <cstdio>

#include "RssCache.h"
#include "RssOrder.h"

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
  teststorage::files.clear();
  retry.content = "Feed excerpt";
  assert(rsscache::mergeAndSaveFeed(feed, "Test", {retry}, merged));
  assert(rsscache::saveArticle(feed, retry, "Full article"));
  assert(rsscache::markReadAndRemove(feed, retry));
  assert(rsscache::isRead(feed, retry));
  assert(!rsscache::loadArticle(feed, retry, text));
  assert(rsscache::loadFeed(feed, title, merged) && merged.empty());
  assert(rsscache::loadFeed(feed, title, merged, nullptr, true));
  assert(merged.size() == 1 && merged[0].content.empty());
  retry.title = "Updated title";
  assert(rsscache::mergeAndSaveFeed(feed, "Test", {retry}, merged));
  assert(merged.size() == 1 && merged[0].content.empty());
  assert(rsscache::isRead(feed, merged[0]));

  std::vector<RssItem> ordered(5);
  ordered[0].guid = "unknown1";
  ordered[1].guid = "old";
  ordered[1].published = "2026-09-01T12:00:00Z";
  ordered[2].guid = "new";
  ordered[2].published = "2026-09-02T12:00:00Z";
  ordered[3].guid = "same";
  ordered[3].published = "2026-09-02T14:00:00+02:00";
  ordered[4].guid = "unknown2";
  ordered[4].published = "invalid";
  rssorder::sort(ordered, true);
  assert(ordered[0].guid == "new" && ordered[1].guid == "same" && ordered[2].guid == "old");
  assert(ordered[3].guid == "unknown1" && ordered[4].guid == "unknown2");
  rssorder::sort(ordered, false);
  assert(ordered[0].guid == "old" && ordered[1].guid == "new" && ordered[2].guid == "same");
  assert(ordered[3].guid == "unknown1" && ordered[4].guid == "unknown2");
  puts("Read visibility, metadata retention and stable chronological ordering: passed");
  puts("RSS cache retry retention, capacity rejection and article persistence: passed");
}
