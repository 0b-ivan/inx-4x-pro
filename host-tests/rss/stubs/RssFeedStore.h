#pragma once
#include <string>
#include <vector>
struct RssFeed {
  std::string name, url, username, password;
};
struct TestFeedStore {
  std::vector<RssFeed> feeds;
  void loadFromFile() {}
  bool hasFeeds() const { return !feeds.empty(); }
  const auto& getFeeds() const { return feeds; }
};
inline TestFeedStore RSS_STORE;
