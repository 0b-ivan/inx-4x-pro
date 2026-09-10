#pragma once
#include <algorithm>
#include <RssParser.h>
#include "RssTime.h"

namespace rssorder {
inline void sort(std::vector<RssItem>& items, bool newestFirst) {
  std::stable_sort(items.begin(), items.end(), [newestFirst](const RssItem& a, const RssItem& b) {
    time_t left = 0, right = 0;
    const bool hasLeft = rsstime::parsePublished(a.published, left);
    const bool hasRight = rsstime::parsePublished(b.published, right);
    if (hasLeft != hasRight) return hasLeft;
    if (!hasLeft || left == right) return false;
    return newestFirst ? left > right : left < right;
  });
}
}
