#pragma once
#include <RssParser.h>

#include <string>

#include "RssFeedStore.h"
#include "RssWifi.h"
namespace rsssync {
std::string fetchArticleText(const RssFeed& feed, const RssItem& item);
enum class SyncState { Idle, Connecting, Feed, Articles, Complete, Incomplete, Cancelled };
struct SyncProgress {
  SyncState state = SyncState::Idle;
  size_t feedsTotal = 0;
  size_t feedsDone = 0;
  size_t feedsFailed = 0;
  size_t articlesTotal = 0;
  size_t articlesSaved = 0;
  size_t articlesFailed = 0;
  unsigned failedFeedMask = 0;
};
// Called on the main loop only. Cancellation takes effect between article downloads.
bool start(const RssFeed* feed = nullptr);
bool active();
void step();
void cancel();
const SyncProgress& progress();
bool syncFeed(const RssFeed& feed);
bool syncAll();
void begin();
void tick(bool immediate = false);
void armSleep();
bool isTimerWake();
}  // namespace rsssync
