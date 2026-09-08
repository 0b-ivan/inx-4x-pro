#include "RssFeedBrowserActivity.h"

#include <FontCacheManager.h>
#include <FreeInkUI.h>
#include <GfxRenderer.h>
#include <HtmlArticleExtractor.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/RssArticleActivity.h"
#include "apps_local/ShelfScreen.h"
#include "apps_local/rss/RssCache.h"
#include "apps_local/rss/RssSync.h"
#include "apps_local/rss/RssTime.h"
#include "apps_local/ui/Toybox.h"
#include "apps_local/ui/ToyboxFonts.h"
#include "apps_local/ui/ToyboxTheme.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr freeink::ui::ActionId ACTION_OPEN_ITEM = 1;

int16_t listRowHeight(const freeink::ui::DrawTarget& target, const freeink::ui::ThemeTokens& tokens) {
  return static_cast<int16_t>(2 * target.lineHeight(tokens.bodyText.font) + toybox::kGutter);
}

}  // namespace

void RssFeedBrowserActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  state = BrowserState::CHECK_WIFI;
  items.clear();
  listValues.clear();
  listItems.clear();
  feedTitle.clear();
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  topIndex = 0;
  visibleRows = 0;
  interactionsReady = false;

  RssCacheInfo cacheInfo;
  if (rsscache::loadFeed(feed, feedTitle, items, &cacheInfo)) {
    LOG_DBG("RSS", "Loaded %u cached entries for %s", static_cast<unsigned>(items.size()), feed.url.c_str());
    rebuildListItems();
    state = BrowserState::BROWSING;
    requestUpdateAndWait();

    // Cache-first: never force Wi-Fi just to browse. If another activity already
    // has Wi-Fi up, refresh an older-than-one-day cache in place.
    if (wifiConnected() && rsscache::isStale(cacheInfo)) fetchFeed();
    return;
  }

  requestUpdate();
  checkAndConnectWifi();
}

void RssFeedBrowserActivity::onExit() {
  Activity::onExit();
  items.clear();
  listValues.clear();
  listItems.clear();
  interactionsReady = false;
}

void RssFeedBrowserActivity::loop() {
  namespace fui = freeink::ui;

  if (state == BrowserState::WIFI_SELECTION) return;

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (wifiConnected()) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchFeed();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  if (state != BrowserState::BROWSING) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool next =
      mappedInput.wasReleased(MappedInputManager::Button::Down) || swipe == MappedInputManager::SwipeDir::Up;
  const bool prev =
      mappedInput.wasReleased(MappedInputManager::Button::Up) || swipe == MappedInputManager::SwipeDir::Down;
  if (next || prev) {
    pageList(next ? 1 : -1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !listItems.empty()) {
    const int index = std::max(0, std::min(topIndex, static_cast<int>(items.size()) - 1));
    openItem(items[static_cast<size_t>(index)]);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);

  if (event.action == ACTION_OPEN_ITEM) {
    const int index = event.value;
    if (index >= 0 && index < static_cast<int>(items.size())) {
      openItem(items[static_cast<size_t>(index)]);
    }
  }
}

void RssFeedBrowserActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const char* headerTitle =
      !feed.name.empty() ? feed.name.c_str() : (!feedTitle.empty() ? feedTitle.c_str() : tr(STR_RSS_READER));

  if (state == BrowserState::BROWSING) {
    renderer.clearScreen();

    auto target = toybox::makeTarget(renderer, toybox::readingFaces());
    const fui::DeviceContext device = target.deviceContext();
    const fui::ThemeTokens& tokens = toybox::themeTokens();
    const fui::InputSnapshot noInput{};
    interactionsReady = false;
    toybox::Frame frame(target, device, noInput, interactions);
    toybox::Screen screen(frame);

    fui::HeaderProps header;
    header.title = headerTitle;
    header.borderEdges = fui::EdgesNone;
    toybox::absoluteChrome(screen);
    toybox::headerBand(screen, header);
    toybox::headerRule(screen);
    screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

    if (listItems.empty()) {
      screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    } else {
      const int16_t rowHeight = listRowHeight(target, tokens);
      visibleRows = fui::listVisibleRows(screen.body(), rowHeight, tokens.listRowGap);
      if (visibleRows > 0) {
        const int pages = shelfui::pageCountFor(static_cast<int>(listItems.size()), visibleRows);
        const int maxTop = pages > 0 ? (pages - 1) * visibleRows : 0;
        if (topIndex > maxTop) topIndex = maxTop;
        if (topIndex < 0) topIndex = 0;
      }

      fui::ListProps list;
      list.items = listItems.data();
      list.count = static_cast<uint16_t>(listItems.size());
      list.topIndex = static_cast<uint16_t>(topIndex);
      list.selectedIndex = -1;
      list.action = ACTION_OPEN_ITEM;
      list.rowHeight = rowHeight;
      list.labelText = tokens.bodyText;
      list.labelText.maxLines = 2;
      list.valueText = tokens.smallText;
      list.valueText.align = fui::TextAlign::Right;
      list.balanceWrappedLabelWithValue = false;
      screen.list(list);
    }

    interactionsReady = true;
    toybox::reportOverflow(interactions, "RSS feed list");

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::WIFI_SELECTION || state == BrowserState::LOADING ||
      state == BrowserState::ARTICLE_LOADING) {
    if (state == BrowserState::ARTICLE_LOADING) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_LOADING));
      auto row = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 20, row.c_str());
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.displayBuffer();
}

void RssFeedBrowserActivity::pageList(const int delta) {
  const int count = static_cast<int>(listItems.size());
  if (count <= 0 || visibleRows <= 0) return;

  const int pages = shelfui::pageCountFor(count, visibleRows);
  if (pages <= 1) return;

  topIndex = shelfui::pageStep(shelfui::pageFor(topIndex, visibleRows), pages, delta) * visibleRows;
  requestUpdate();
}

void RssFeedBrowserActivity::fetchFeed() {
  if (feed.url.empty()) {
    if (!items.empty()) {
      state = BrowserState::BROWSING;
      requestUpdate();
      return;
    }
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_FEED_URL);
    requestUpdate();
    return;
  }

  const bool hasFallback = !items.empty();
  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());
  RssParser parser;
  if (!HttpDownloader::fetchUrl(
          feed.url, [&parser](const uint8_t* data, const size_t len) { return parser.write(data, len) == len; },
          feed.username, feed.password)) {
    LOG_ERR("RSS", "Feed request failed (HTTP %d)", HttpDownloader::lastStatus());
    if (hasFallback) {
      state = BrowserState::BROWSING;
      requestUpdate();
      return;
    }
    state = BrowserState::ERROR;
    errorMessage = HttpDownloader::lastStatus() == 401 ? tr(STR_AUTH_FAILED) : tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }
  parser.flush();

  if (!parser) {
    if (hasFallback) {
      state = BrowserState::BROWSING;
      requestUpdate();
      return;
    }
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  const std::string freshTitle = parser.getFeedTitle();
  const std::vector<RssItem> freshItems = std::move(parser).getItems();
  std::vector<RssItem> mergedItems;
  RssCacheInfo cacheInfo;
  const bool cacheSaved = rsscache::mergeAndSaveFeed(feed, freshTitle, freshItems, mergedItems, &cacheInfo);
  if (!cacheSaved) LOG_ERR("RSS", "Feed loaded, but cache write failed");

  feedTitle = freshTitle.empty() ? feedTitle : freshTitle;
  items = mergedItems.empty() && !freshItems.empty() ? freshItems : std::move(mergedItems);
  topIndex = 0;
  visibleRows = 0;
  rebuildListItems();

  state = BrowserState::BROWSING;
  requestUpdate();
}

void RssFeedBrowserActivity::rebuildListItems() {
  listValues.clear();
  listValues.reserve(items.size());
  for (const auto& item : items)
    listValues.push_back(rsstime::formatPublished(item.published, SETTINGS.clockUtcOffsetQ));

  listItems.clear();
  listItems.reserve(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    freeink::ui::ListItem row;
    row.label = !items[i].title.empty() ? items[i].title.c_str()
                                        : (!items[i].link.empty() ? items[i].link.c_str() : tr(STR_RSS_READER));
    row.value =
        !listValues[i].empty() ? listValues[i].c_str() : (!items[i].author.empty() ? items[i].author.c_str() : "");
    row.actionValue = static_cast<int16_t>(i);
    listItems.push_back(row);
  }
}

void RssFeedBrowserActivity::openItem(const RssItem& item) {
  RssItem article = item;
  std::string cachedArticle;
  const bool hasCachedArticle = rsscache::loadArticle(feed, item, cachedArticle);
  if (hasCachedArticle) article.content = cachedArticle;

  const bool canFetchArticle =
      !hasCachedArticle && (item.link.starts_with("http://") || item.link.starts_with("https://"));
  if (canFetchArticle) {
    state = BrowserState::ARTICLE_LOADING;
    statusMessage = item.title.empty() ? tr(STR_LOADING) : item.title;
    requestUpdateAndWait();

    rsssync::WifiSession wifi;
    auto* fontCache = renderer.getFontCacheManager();
    if (fontCache) fontCache->clearCache();
    std::string extracted = wifi.connect() ? rsssync::fetchArticleText(feed, item) : std::string{};
    if (!extracted.empty()) {
      article.content = extracted;
      if (!rsscache::saveArticle(feed, item, extracted)) LOG_ERR("RSS", "Could not cache article text");
    }
  }

  state = BrowserState::BROWSING;
  startActivityForResult(std::make_unique<RssArticleActivity>(renderer, mappedInput, article),
                         [this](const ActivityResult&) { requestUpdate(); });
}

bool RssFeedBrowserActivity::wifiConnected() const {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void RssFeedBrowserActivity::checkAndConnectWifi() {
  if (wifiConnected()) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed();
    return;
  }
  launchWifiSelection();
}

void RssFeedBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void RssFeedBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchFeed();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
