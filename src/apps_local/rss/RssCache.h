#pragma once

#include <RssParser.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "RssFeedStore.h"

struct RssCacheInfo {
  bool available = false;
  uint32_t lastSyncEpoch = 0;
  size_t itemCount = 0;
};

namespace rsscache {

static constexpr size_t MAX_CACHED_ITEMS = 80;
static constexpr uint32_t DEFAULT_MAX_AGE_SECONDS = 24U * 60U * 60U;

bool loadFeed(const RssFeed& feed, std::string& feedTitle, std::vector<RssItem>& items, RssCacheInfo* info = nullptr,
              bool includeRead = false);

bool isRead(const RssFeed& feed, const RssItem& item);
bool markReadAndRemove(const RssFeed& feed, const RssItem& item);

// Merges fresh entries in front of the existing cache, keeps older unique
// entries up to MAX_CACHED_ITEMS, writes the merged result to SD and returns
// the merged list even when the SD write fails.
bool mergeAndSaveFeed(const RssFeed& feed, const std::string& freshTitle, const std::vector<RssItem>& freshItems,
                      std::vector<RssItem>& mergedItems, RssCacheInfo* info = nullptr);

RssCacheInfo getInfo(const RssFeed& feed);
bool isStale(const RssCacheInfo& info, uint32_t maxAgeSeconds = DEFAULT_MAX_AGE_SECONDS);

bool sameItem(const RssItem& lhs, const RssItem& rhs);
bool articlePending(const RssFeed& feed, const RssItem& item);
bool markArticlePending(const RssFeed& feed, const RssItem& item);

bool loadArticle(const RssFeed& feed, const RssItem& item, std::string& text);
bool saveArticle(const RssFeed& feed, const RssItem& item, const std::string& text);

// Removes the feed index. Per-article files are content-addressed and may be
// reused when the same feed URL is configured again.
bool clearFeed(const RssFeed& feed);

}  // namespace rsscache
