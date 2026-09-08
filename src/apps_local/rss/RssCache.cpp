#include "RssCache.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {
constexpr char CACHE_DIR[] = "/.crosspoint/rss-cache";
constexpr uint32_t FEED_MAGIC = 0x31534352U;     // RSC1
constexpr uint32_t ARTICLE_MAGIC = 0x31414352U;  // RCA1
constexpr uint16_t CACHE_VERSION = 1;
constexpr size_t MAX_FEED_TITLE_BYTES = 512;
constexpr size_t MAX_URL_BYTES = 2048;
constexpr size_t MAX_TITLE_BYTES = 4096;
constexpr size_t MAX_LINK_BYTES = 4096;
constexpr size_t MAX_AUTHOR_BYTES = 1024;
constexpr size_t MAX_PUBLISHED_BYTES = 256;
constexpr size_t MAX_GUID_BYTES = 4096;
constexpr size_t MAX_FEED_CONTENT_BYTES = 24 * 1024;
constexpr size_t MAX_ARTICLE_BYTES = 64 * 1024;

uint32_t fnv1aAppend(uint32_t hash, const char* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint8_t>(data[i]);
    hash *= 16777619U;
  }
  return hash;
}

uint32_t hashString(const std::string& value) { return fnv1aAppend(2166136261U, value.data(), value.size()); }

uint32_t itemHash(const RssItem& item) {
  uint32_t hash = 2166136261U;
  const std::string* parts[] = {&item.guid, &item.link, &item.title, &item.published};
  for (const auto* part : parts) {
    hash = fnv1aAppend(hash, part->data(), part->size());
    constexpr char separator = '\x1f';
    hash = fnv1aAppend(hash, &separator, 1);
  }
  return hash;
}

std::string feedPath(const RssFeed& feed, const char* suffix) {
  char path[80];
  snprintf(path, sizeof(path), "%s/%08lx%s", CACHE_DIR, static_cast<unsigned long>(hashString(feed.url)), suffix);
  return path;
}

std::string articlePath(const RssFeed& feed, const RssItem& item, const char* suffix) {
  char path[96];
  snprintf(path, sizeof(path), "%s/%08lx-%08lx%s", CACHE_DIR, static_cast<unsigned long>(hashString(feed.url)),
           static_cast<unsigned long>(itemHash(item)), suffix);
  return path;
}

template <typename T>
bool writePod(HalFile& file, const T& value) {
  return file.write(&value, sizeof(value)) == sizeof(value);
}

template <typename T>
bool readPod(HalFile& file, T& value) {
  return file.read(&value, sizeof(value)) == static_cast<int>(sizeof(value));
}

bool writeString(HalFile& file, const std::string& value, const size_t maxBytes) {
  const uint32_t length = static_cast<uint32_t>(std::min(value.size(), maxBytes));
  if (!writePod(file, length)) return false;
  return length == 0 || file.write(value.data(), length) == length;
}

bool readString(HalFile& file, std::string& value, const size_t maxBytes) {
  uint32_t length = 0;
  if (!readPod(file, length) || length > maxBytes) return false;
  value.clear();
  if (length == 0) return true;
  value.resize(length);
  if (file.read(value.data(), length) != static_cast<int>(length)) {
    value.clear();
    return false;
  }
  return true;
}

bool sameItem(const RssItem& lhs, const RssItem& rhs) {
  if (!lhs.guid.empty() && !rhs.guid.empty()) return lhs.guid == rhs.guid;
  if (!lhs.link.empty() && !rhs.link.empty()) return lhs.link == rhs.link;
  return !lhs.title.empty() && lhs.title == rhs.title && lhs.published == rhs.published;
}

RssItem boundedItem(const RssItem& source) {
  RssItem result = source;
  if (result.title.size() > MAX_TITLE_BYTES) result.title.resize(MAX_TITLE_BYTES);
  if (result.link.size() > MAX_LINK_BYTES) result.link.resize(MAX_LINK_BYTES);
  if (result.author.size() > MAX_AUTHOR_BYTES) result.author.resize(MAX_AUTHOR_BYTES);
  if (result.published.size() > MAX_PUBLISHED_BYTES) result.published.resize(MAX_PUBLISHED_BYTES);
  if (result.content.size() > MAX_FEED_CONTENT_BYTES) result.content.resize(MAX_FEED_CONTENT_BYTES);
  if (result.guid.size() > MAX_GUID_BYTES) result.guid.resize(MAX_GUID_BYTES);
  return result;
}

uint32_t currentEpoch() {
  const time_t now = time(nullptr);
  // A freshly booted device can report the Unix epoch until SNTP has run.
  if (now < 1609459200) return 0;
  return static_cast<uint32_t>(now);
}

bool readFeedHeader(HalFile& file, uint16_t& count, uint32_t& lastSyncEpoch, std::string& cachedUrl) {
  uint32_t magic = 0;
  uint16_t version = 0;
  if (!readPod(file, magic) || !readPod(file, version) || !readPod(file, count) || !readPod(file, lastSyncEpoch)) {
    return false;
  }
  if (magic != FEED_MAGIC || version != CACHE_VERSION || count > rsscache::MAX_CACHED_ITEMS) return false;
  return readString(file, cachedUrl, MAX_URL_BYTES);
}

bool writeFeedFile(const RssFeed& feed, const std::string& feedTitle, const std::vector<RssItem>& items,
                   const uint32_t lastSyncEpoch) {
  if (!Storage.ensureDirectoryExists(CACHE_DIR)) return false;

  const std::string finalPath = feedPath(feed, ".bin");
  const std::string tempPath = feedPath(feed, ".tmp");
  HalFile file;
  if (!Storage.openFileForWrite("RSS", tempPath, file)) return false;

  const uint16_t count = static_cast<uint16_t>(std::min(items.size(), rsscache::MAX_CACHED_ITEMS));
  bool ok = writePod(file, FEED_MAGIC) && writePod(file, CACHE_VERSION) && writePod(file, count) &&
            writePod(file, lastSyncEpoch) && writeString(file, feed.url, MAX_URL_BYTES) &&
            writeString(file, feedTitle, MAX_FEED_TITLE_BYTES);

  for (size_t i = 0; ok && i < count; ++i) {
    const RssItem bounded = boundedItem(items[i]);
    ok = writeString(file, bounded.title, MAX_TITLE_BYTES) && writeString(file, bounded.link, MAX_LINK_BYTES) &&
         writeString(file, bounded.author, MAX_AUTHOR_BYTES) &&
         writeString(file, bounded.published, MAX_PUBLISHED_BYTES) &&
         writeString(file, bounded.content, MAX_FEED_CONTENT_BYTES) && writeString(file, bounded.guid, MAX_GUID_BYTES);
  }

  file.flush();
  file.close();  // close before rename/remove of the same paths
  if (!ok) {
    Storage.remove(tempPath.c_str());
    return false;
  }

  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}
}  // namespace

namespace rsscache {

bool loadFeed(const RssFeed& feed, std::string& feedTitle, std::vector<RssItem>& items, RssCacheInfo* info) {
  feedTitle.clear();
  items.clear();
  if (info) *info = RssCacheInfo{};

  HalFile file;
  const std::string path = feedPath(feed, ".bin");
  if (!Storage.openFileForRead("RSS", path, file)) return false;

  uint16_t count = 0;
  uint32_t lastSyncEpoch = 0;
  std::string cachedUrl;
  if (!readFeedHeader(file, count, lastSyncEpoch, cachedUrl) || cachedUrl != feed.url ||
      !readString(file, feedTitle, MAX_FEED_TITLE_BYTES)) {
    LOG_ERR("RSS", "Ignoring invalid RSS cache: %s", path.c_str());
    return false;
  }

  items.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    RssItem item;
    if (!readString(file, item.title, MAX_TITLE_BYTES) || !readString(file, item.link, MAX_LINK_BYTES) ||
        !readString(file, item.author, MAX_AUTHOR_BYTES) || !readString(file, item.published, MAX_PUBLISHED_BYTES) ||
        !readString(file, item.content, MAX_FEED_CONTENT_BYTES) || !readString(file, item.guid, MAX_GUID_BYTES)) {
      items.clear();
      feedTitle.clear();
      LOG_ERR("RSS", "RSS cache ended unexpectedly: %s", path.c_str());
      return false;
    }
    items.push_back(std::move(item));
  }

  if (info) {
    info->available = true;
    info->lastSyncEpoch = lastSyncEpoch;
    info->itemCount = items.size();
  }
  return true;
}

bool mergeAndSaveFeed(const RssFeed& feed, const std::string& freshTitle, const std::vector<RssItem>& freshItems,
                      std::vector<RssItem>& mergedItems, RssCacheInfo* info) {
  std::string oldTitle;
  std::vector<RssItem> oldItems;
  RssCacheInfo oldInfo;
  loadFeed(feed, oldTitle, oldItems, &oldInfo);

  mergedItems.clear();
  mergedItems.reserve(MAX_CACHED_ITEMS);

  auto appendUnique = [&](RssItem candidate) {
    if (mergedItems.size() >= MAX_CACHED_ITEMS) return;
    const auto duplicate = std::find_if(mergedItems.begin(), mergedItems.end(),
                                        [&](const RssItem& existing) { return sameItem(existing, candidate); });
    if (duplicate == mergedItems.end()) mergedItems.push_back(boundedItem(candidate));
  };

  for (const auto& fresh : freshItems) {
    RssItem candidate = fresh;
    const auto previous = std::find_if(oldItems.begin(), oldItems.end(),
                                       [&](const RssItem& existing) { return sameItem(existing, fresh); });
    if (previous != oldItems.end() && previous->content.size() > candidate.content.size()) {
      candidate.content = previous->content;
    }
    appendUnique(std::move(candidate));
    if (mergedItems.size() >= MAX_CACHED_ITEMS) break;
  }

  for (const auto& old : oldItems) {
    appendUnique(old);
    if (mergedItems.size() >= MAX_CACHED_ITEMS) break;
  }

  const std::string title = !freshTitle.empty() ? freshTitle : oldTitle;
  const uint32_t lastSyncEpoch = currentEpoch();
  const bool saved = writeFeedFile(feed, title, mergedItems, lastSyncEpoch);
  if (!saved) LOG_ERR("RSS", "Could not persist RSS cache for %s", feed.url.c_str());

  if (info) {
    info->available = saved || oldInfo.available;
    info->lastSyncEpoch = saved ? lastSyncEpoch : oldInfo.lastSyncEpoch;
    info->itemCount = mergedItems.size();
  }
  return saved;
}

RssCacheInfo getInfo(const RssFeed& feed) {
  RssCacheInfo info;
  HalFile file;
  const std::string path = feedPath(feed, ".bin");
  if (!Storage.openFileForRead("RSS", path, file)) return info;

  uint16_t count = 0;
  uint32_t lastSyncEpoch = 0;
  std::string cachedUrl;
  if (!readFeedHeader(file, count, lastSyncEpoch, cachedUrl) || cachedUrl != feed.url) return info;

  info.available = true;
  info.lastSyncEpoch = lastSyncEpoch;
  info.itemCount = count;
  return info;
}

bool isStale(const RssCacheInfo& info, const uint32_t maxAgeSeconds) {
  if (!info.available || info.lastSyncEpoch == 0) return true;
  const uint32_t now = currentEpoch();
  if (now == 0) return false;
  if (now < info.lastSyncEpoch) return true;
  return now - info.lastSyncEpoch >= maxAgeSeconds;
}

bool loadArticle(const RssFeed& feed, const RssItem& item, std::string& text) {
  text.clear();
  HalFile file;
  const std::string path = articlePath(feed, item, ".article");
  if (!Storage.openFileForRead("RSS", path, file)) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint32_t storedItemHash = 0;
  if (!readPod(file, magic) || !readPod(file, version) || !readPod(file, storedItemHash) || magic != ARTICLE_MAGIC ||
      version != CACHE_VERSION || storedItemHash != itemHash(item) || !readString(file, text, MAX_ARTICLE_BYTES)) {
    text.clear();
    return false;
  }
  return !text.empty();
}

bool saveArticle(const RssFeed& feed, const RssItem& item, const std::string& text) {
  if (text.empty() || !Storage.ensureDirectoryExists(CACHE_DIR)) return false;

  const std::string finalPath = articlePath(feed, item, ".article");
  const std::string tempPath = articlePath(feed, item, ".article.tmp");
  HalFile file;
  if (!Storage.openFileForWrite("RSS", tempPath, file)) return false;

  const uint32_t hash = itemHash(item);
  const bool ok = writePod(file, ARTICLE_MAGIC) && writePod(file, CACHE_VERSION) && writePod(file, hash) &&
                  writeString(file, text, MAX_ARTICLE_BYTES);
  file.flush();
  file.close();  // close before rename/remove of the same paths

  if (!ok) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool clearFeed(const RssFeed& feed) {
  const std::string path = feedPath(feed, ".bin");
  if (!Storage.exists(path.c_str())) return true;
  return Storage.remove(path.c_str());
}

}  // namespace rsscache
