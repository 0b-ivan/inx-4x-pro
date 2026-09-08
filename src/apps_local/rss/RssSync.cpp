#include "RssSync.h"

#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <ctime>
#include <optional>

#include "CrossPointSettings.h"
#include "RssCache.h"
#include "RssSyncSettings.h"
#include "RssTime.h"
#include "network/HttpDownloader.h"
#ifndef SIMULATOR
#include <esp_sleep.h>
#endif

namespace rsssync {
namespace {
SyncProgress current;
struct Job {
  HalPowerManager::Lock powerLock;
  WifiSession wifi;
  // A bounded feed snapshot keeps credentials and indexes stable across loop iterations.
  std::vector<RssFeed> feeds;
  std::vector<RssItem> items;
  size_t itemIndex = 0;
  bool feedFailed = false;
  int64_t scheduledDay = -1;
  time_t started = time(nullptr);
};
std::optional<Job> job;
unsigned long nextStepAfter = 0;

bool needsArticle(const RssFeed& feed, const RssItem& item) {
  time_t published = 0;
  return rsscache::articlePending(feed, item) || (rsstime::parsePublished(item.published, published) &&
                                                  rsstime::localDay(published, SETTINGS.clockUtcOffsetQ) ==
                                                      rsstime::localDay(job->started, SETTINGS.clockUtcOffsetQ));
}

void finish(SyncState state) {
  if (state == SyncState::Complete && job->scheduledDay >= 0) {
    auto& settings = RSS_SYNC_SETTINGS;
    const auto oldDay = settings.lastDay;
    settings.lastDay = job->scheduledDay;
    if (!settings.saveToFile()) {
      settings.lastDay = oldDay;
      state = SyncState::Incomplete;
    }
  }
  current.state = state;
  job.reset();  // Releases only Wi-Fi owned by this sync and its power lock.
}

void finishFeed() {
  if (job->feedFailed) {
    ++current.feedsFailed;
    current.failedFeedMask |= 1U << current.feedsDone;
    LOG_ERR("RSS", "Feed sync incomplete");
  }
  ++current.feedsDone;
  job->items.clear();
  job->itemIndex = 0;
  job->feedFailed = false;
  if (current.feedsDone == current.feedsTotal) {
    finish(current.feedsFailed == 0 && current.articlesSaved == current.articlesTotal ? SyncState::Complete
                                                                                      : SyncState::Incomplete);
  } else {
    current.state = SyncState::Feed;
  }
}

bool prepareFeed(const RssFeed& feed) {
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
  // Persist every planned download before replacing the index or yielding to cancel.
  for (const auto& item : parser.getItems()) {
    const bool isNew = std::none_of(oldItems.begin(), oldItems.end(),
                                    [&](const RssItem& old) { return rsscache::sameItem(old, item); });
    std::string text;
    if ((isNew || needsArticle(feed, item)) && !rsscache::loadArticle(feed, item, text) &&
        !rsscache::markArticlePending(feed, item))
      return false;
  }
  for (const auto& item : oldItems) {
    std::string text;
    if (needsArticle(feed, item) && !rsscache::loadArticle(feed, item, text) &&
        !rsscache::markArticlePending(feed, item))
      return false;
  }
  if (!rsscache::mergeAndSaveFeed(feed, parser.getFeedTitle(), parser.getItems(), job->items)) return false;
  // Compact the existing cache vector in place; no second article queue is allocated.
  auto& items = job->items;
  items.erase(std::remove_if(items.begin(), items.end(),
                             [&](const RssItem& item) {
                               std::string text;
                               return !needsArticle(feed, item) || rsscache::loadArticle(feed, item, text);
                             }),
              items.end());
  current.articlesTotal += items.size();
  return true;
}
}  // namespace

const SyncProgress& progress() { return current; }
bool active() { return job.has_value(); }
bool start(const RssFeed* feed) {
  if (active() || (!feed && !RSS_STORE.hasFeeds())) return false;
  job.emplace();
  job->feeds.reserve(feed ? 1 : RSS_STORE.getFeeds().size());
  if (feed)
    job->feeds.push_back(*feed);
  else
    for (const auto& entry : RSS_STORE.getFeeds()) job->feeds.push_back(entry);
  current = {};
  current.feedsTotal = job->feeds.size();
  current.state = SyncState::Connecting;
  nextStepAfter = millis();
  return true;
}
void cancel() {
  if (active()) finish(SyncState::Cancelled);
}
void step() {
  if (!active()) return;
  if (current.state == SyncState::Connecting) {
    if (!job->wifi.connect()) {
      current.feedsFailed = current.feedsTotal;
      current.failedFeedMask = (1U << current.feedsTotal) - 1;
      finish(SyncState::Incomplete);
    } else
      current.state = SyncState::Feed;
    return;
  }
  const auto& feed = job->feeds[current.feedsDone];
  if (current.state == SyncState::Feed) {
    if (!prepareFeed(feed)) {
      job->feedFailed = true;
      finishFeed();
    } else
      current.state = SyncState::Articles;
    return;
  }
  if (job->itemIndex == job->items.size()) {
    finishFeed();
    return;
  }
  const auto& item = job->items[job->itemIndex++];
  std::string text;
  if (rsscache::loadArticle(feed, item, text)) {
    ++current.articlesSaved;
    return;
  }
  bool saved = false;
  if (rsscache::markArticlePending(feed, item)) {
    text = (item.link.starts_with("http://") || item.link.starts_with("https://")) ? fetchArticleText(feed, item)
                                                                                   : item.content;
    saved = !text.empty() && rsscache::saveArticle(feed, item, text);
  }
  if (saved)
    ++current.articlesSaved;
  else {
    ++current.articlesFailed;
    job->feedFailed = true;
  }
}
bool syncFeed(const RssFeed& feed) {
  if (!start(&feed)) return false;
  while (active()) {
    step();
    delay(1);
  }
  return current.state == SyncState::Complete;
}
bool syncAll() {
  if (!start()) return false;
  while (active()) {
    step();
    delay(1);
  }
  return current.state == SyncState::Complete;
}
void begin() {
  RSS_STORE.loadFromFile();
  RSS_SYNC_SETTINGS.loadFromFile();
}
void tick(bool immediate) {
  if (active()) {
    if (static_cast<long>(millis() - nextStepAfter) >= 0) {
      step();
      nextStepAfter = millis() + 250;
    }
    return;
  }
  static unsigned long lastCheck = 0;
  if (!immediate && millis() - lastCheck < 60000) return;
  lastCheck = millis();
  const time_t now = time(nullptr);
  auto& settings = RSS_SYNC_SETTINGS;
  if (!settings.enabled || !RSS_STORE.hasFeeds() || now < 1609459200) return;
  const int64_t next =
      rsstime::nextSync(now, SETTINGS.clockUtcOffsetQ, settings.minute, settings.lastDay, settings.lastAttempt);
  if (!next || now < next) return;
  const auto oldAttempt = settings.lastAttempt;
  settings.lastAttempt = now;
  if (!settings.saveToFile()) {
    settings.lastAttempt = oldAttempt;
    return;
  }
  if (!start()) return;
  job->scheduledDay = rsstime::localDay(now, SETTINGS.clockUtcOffsetQ);
  // Timer boot returns directly to sleep, so drain the same steps before returning.
  if (immediate)
    while (active()) {
      step();
      delay(1);
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
