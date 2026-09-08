#pragma once
#include <RssParser.h>

#include <string>
#include <utility>
#include <vector>

#include "apps_local/ui/ToyboxScreen.h"
#include "RssFeedStore.h"
#include "activities/Activity.h"

class RssFeedBrowserActivity final : public Activity {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, ARTICLE_LOADING, ERROR };

  RssFeedBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RssFeed feed)
      : Activity("RssFeedBrowser", renderer, mappedInput), feed(std::move(feed)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  BrowserState state = BrowserState::CHECK_WIFI;
  RssFeed feed;
  std::vector<RssItem> items;
  std::string feedTitle;
  std::string errorMessage;
  std::string statusMessage;
  int topIndex = 0;
  int visibleRows = 0;
  std::vector<freeink::ui::ListItem> listItems;

  toybox::Interactions interactions;
  bool interactionsReady = false;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed();
  void openItem(const RssItem& item);
  std::string fetchArticleText(const RssItem& item);
  void pageList(int delta);
  bool preventAutoSleep() override { return true; }
};
