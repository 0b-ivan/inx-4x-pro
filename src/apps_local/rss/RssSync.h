#pragma once
#include <RssParser.h>

#include <string>

#include "RssFeedStore.h"
#include "RssWifi.h"
namespace rsssync {
std::string fetchArticleText(const RssFeed& feed, const RssItem& item);
bool syncFeed(const RssFeed& feed);
bool syncAll();
void begin();
void tick(bool immediate = false);
void armSleep();
bool isTimerWake();
}  // namespace rsssync
