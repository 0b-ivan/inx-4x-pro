#include "RssSync.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>

#include "CrossPointSettings.h"
#include "RssCache.h"
#include "RssSyncSettings.h"
#include "RssTime.h"
#include "network/HttpDownloader.h"
#ifndef SIMULATOR
#include <esp_sleep.h>
#endif

namespace rsssync {
bool syncFeed(const RssFeed& feed) {
  RssParser parser;
  size_t received = 0;
  const bool fetched = HttpDownloader::fetchUrl(
      feed.url,
      [&](const uint8_t* data, size_t length) {
        if (length > 512 * 1024 - received) return false;
        received += length;
        return parser.write(data, length) == length && !parser.error();
      },
      feed.username, feed.password);
  parser.flush();
  if (!fetched || !parser || (parser.getItems().empty() && parser.getFeedTitle().empty())) return false;
  std::string oldTitle;
  std::vector<RssItem> oldItems;
  rsscache::loadFeed(feed, oldTitle, oldItems);
  // Record new entries before replacing the index, so failures remain retryable
  // after midnight, restart, or disappearance from the live feed.
  for (const auto& item : parser.getItems()) {
    const bool isNew = std::none_of(oldItems.begin(), oldItems.end(),
                                    [&](const RssItem& old) { return rsscache::sameItem(old, item); });
    std::string text;
    if (isNew && !rsscache::loadArticle(feed, item, text) && !rsscache::markArticlePending(feed, item)) return false;
  }
  oldItems.clear();
  if (!rsscache::mergeAndSaveFeed(feed, parser.getFeedTitle(), parser.getItems(), oldItems)) return false;
  const auto now = time(nullptr);
  bool complete = true;
  // Process one full article at a time, including retained entries with retries.
  for (const auto& item : oldItems) {
    time_t published = 0;
    const bool today =
        rsstime::parsePublished(item.published, published) &&
        rsstime::localDay(published, SETTINGS.clockUtcOffsetQ) == rsstime::localDay(now, SETTINGS.clockUtcOffsetQ);
    if (!today && !rsscache::articlePending(feed, item)) continue;
    std::string text;
    if (rsscache::loadArticle(feed, item, text)) continue;
    if (!rsscache::markArticlePending(feed, item)) {
      complete = false;
      continue;
    }
    text = (item.link.starts_with("http://") || item.link.starts_with("https://")) ? fetchArticleText(feed, item)
                                                                                   : item.content;
    if (text.empty() || !rsscache::saveArticle(feed, item, text)) complete = false;
    delay(1);
  }
  return complete;
}
bool syncAll() {
  HalPowerManager::Lock powerLock;
  WifiSession wifi;
  if (!wifi.connect()) return false;
  bool complete = true;
  for (const auto& feed : RSS_STORE.getFeeds()) {
    if (!syncFeed(feed)) {
      LOG_ERR("RSS", "Feed sync incomplete");
      complete = false;
    }
  }
  return complete;
}
void begin() {
  RSS_STORE.loadFromFile();
  RSS_SYNC_SETTINGS.loadFromFile();
}
void tick(bool immediate) {
  static unsigned long lastCheck = 0;
  if (!immediate && millis() - lastCheck < 60000) return;
  lastCheck = millis();
  const time_t now = time(nullptr);
  auto& settings = RSS_SYNC_SETTINGS;
  if (!settings.enabled || !RSS_STORE.hasFeeds() || now < 1609459200) return;
  const int64_t next =
      rsstime::nextSync(now, SETTINGS.clockUtcOffsetQ, settings.minute, settings.lastDay, settings.lastAttempt);
  if (!next || now < next) return;
  const int64_t day = rsstime::localDay(now, SETTINGS.clockUtcOffsetQ);
  settings.lastAttempt = now;
  if (!settings.saveToFile()) return;
  if (syncAll()) {
    settings.lastDay = day;
    settings.saveToFile();
  }
}
bool isTimerWake() {
#ifndef SIMULATOR
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#else
  return false;
#endif
}
void armSleep() {
#ifndef SIMULATOR
  const auto& settings = RSS_SYNC_SETTINGS;
  const time_t now = time(nullptr);
  if (!settings.enabled || !RSS_STORE.hasFeeds() || now < 1609459200) return;
  const int64_t next = std::max<int64_t>(now + 60, rsstime::nextSync(now, SETTINGS.clockUtcOffsetQ, settings.minute,
                                                                     settings.lastDay, settings.lastAttempt));
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(next - now) * 1000000ULL);
#endif
}
}  // namespace rsssync
