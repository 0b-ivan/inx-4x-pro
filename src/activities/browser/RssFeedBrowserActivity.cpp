#include "RssFeedBrowserActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HtmlArticleExtractor.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/RssArticleActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
constexpr size_t MAX_ARTICLE_HTML_BYTES = 40 * 1024;

bool startsWithAtCaseInsensitive(const std::string& text, size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  if (pos + len > text.size()) return false;
  for (size_t i = 0; i < len; i++) {
    const char lhs = static_cast<char>(tolower(static_cast<unsigned char>(text[pos + i])));
    const char rhs = static_cast<char>(tolower(static_cast<unsigned char>(prefix[i])));
    if (lhs != rhs) return false;
  }
  return true;
}

size_t findCaseInsensitive(const std::string& text, const char* needle, size_t start = 0) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) return start <= text.size() ? start : std::string::npos;
  if (needleLen > text.size() || start > text.size() - needleLen) return std::string::npos;
  for (size_t pos = start; pos <= text.size() - needleLen; pos++) {
    if (startsWithAtCaseInsensitive(text, pos, needle)) return pos;
  }
  return std::string::npos;
}

std::string resolveArticleUrl(const std::string& baseUrl, const std::string& href) {
  if (href.empty()) return {};
  if (href.starts_with("http://") || href.starts_with("https://")) return href;
  const size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd == std::string::npos) return {};
  if (href.starts_with("//")) return baseUrl.substr(0, schemeEnd) + ":" + href;

  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = baseUrl.find('/', hostStart);
  const std::string origin = pathStart == std::string::npos ? baseUrl : baseUrl.substr(0, pathStart);
  if (href[0] == '/') return origin + href;

  size_t queryStart = baseUrl.find_first_of("?#", hostStart);
  if (queryStart == std::string::npos) queryStart = baseUrl.size();
  size_t dirEnd = baseUrl.rfind('/', queryStart);
  if (dirEnd == std::string::npos || dirEnd < hostStart) return origin + "/" + href;
  return baseUrl.substr(0, dirEnd + 1) + href;
}

std::string extractAmpHtmlUrl(const std::string& html, const std::string& baseUrl) {
  size_t pos = 0;
  while ((pos = findCaseInsensitive(html, "<link", pos)) != std::string::npos) {
    const size_t tagEnd = html.find('>', pos + 5);
    if (tagEnd == std::string::npos) break;
    const size_t tagLen = tagEnd - pos + 1;
    if (findCaseInsensitive(html.substr(pos, tagLen), "amphtml") == std::string::npos) {
      pos = tagEnd + 1;
      continue;
    }

    const size_t hrefPos = findCaseInsensitive(html, "href", pos);
    if (hrefPos == std::string::npos || hrefPos > tagEnd) {
      pos = tagEnd + 1;
      continue;
    }
    size_t valueStart = html.find('=', hrefPos + 4);
    if (valueStart == std::string::npos || valueStart > tagEnd) {
      pos = tagEnd + 1;
      continue;
    }
    valueStart++;
    while (valueStart < tagEnd && isspace(static_cast<unsigned char>(html[valueStart]))) valueStart++;
    if (valueStart >= tagEnd) {
      pos = tagEnd + 1;
      continue;
    }

    const char quote = (html[valueStart] == '"' || html[valueStart] == '\'') ? html[valueStart++] : '\0';
    size_t valueEnd = valueStart;
    while (valueEnd < tagEnd) {
      if ((quote && html[valueEnd] == quote) ||
          (!quote && (isspace(static_cast<unsigned char>(html[valueEnd])) || html[valueEnd] == '>'))) {
        break;
      }
      valueEnd++;
    }
    return resolveArticleUrl(baseUrl, html.substr(valueStart, valueEnd - valueStart));
  }
  return {};
}

std::string wikipediaRenderUrl(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return {};
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  if (pathStart == std::string::npos) return {};

  const std::string host = url.substr(hostStart, pathStart - hostStart);
  if (host.find("wikipedia.org") == std::string::npos) return {};
  if (url.compare(pathStart, 6, "/wiki/") != 0) return {};

  size_t titleEnd = url.find_first_of("?#", pathStart + 6);
  if (titleEnd == std::string::npos) titleEnd = url.size();
  if (titleEnd <= pathStart + 6) return {};

  const std::string title = url.substr(pathStart + 6, titleEnd - pathStart - 6);
  return url.substr(0, schemeEnd + 3) + host + "/w/index.php?title=" + title + "&action=render";
}

bool fetchArticleHtml(const std::string& url, const std::string& username, const std::string& password,
                     std::string& outHtml) {
  outHtml.clear();
  outHtml.reserve(4096);
  size_t total = 0;
  bool truncated = false;
  const bool fetched = HttpDownloader::fetchUrl(
      url,
      [&](const uint8_t* data, size_t len) {
        if (total >= MAX_ARTICLE_HTML_BYTES) {
          truncated = true;
          return false;
        }
        const size_t copyLen = std::min(len, MAX_ARTICLE_HTML_BYTES - total);
        if (copyLen > 0) {
          outHtml.append(reinterpret_cast<const char*>(data), copyLen);
          total += copyLen;
        }
        if (total >= MAX_ARTICLE_HTML_BYTES) {
          truncated = true;
          return false;
        }
        return true;
      },
      username, password);

  if (truncated) LOG_DBG("RSS", "Article HTML truncated at %u bytes: %s", static_cast<unsigned>(total), url.c_str());
  return fetched || !outHtml.empty();
}
}  // namespace

void RssFeedBrowserActivity::onEnter() {
  Activity::onEnter();

  state = BrowserState::CHECK_WIFI;
  items.clear();
  feedTitle.clear();
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void RssFeedBrowserActivity::onExit() {
  Activity::onExit();
  items.clear();
}

void RssFeedBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) return;

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!items.empty()) openItem(items[selectorIndex]);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }

    if (!items.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, items.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, items.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void RssFeedBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* headerTitle =
      !feed.name.empty() ? feed.name.c_str() : (!feedTitle.empty() ? feedTitle.c_str() : tr(STR_RSS_READER));
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING || state == BrowserState::ARTICLE_LOADING) {
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

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (items.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_NO_ENTRIES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, LIST_TOP + (selectorIndex % PAGE_ITEMS) * ROW_HEIGHT - 3, pageWidth - 1, ROW_HEIGHT);

    for (size_t i = pageStartIndex; i < items.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& item = items[i];
      std::string displayText = item.title;
      if (!item.published.empty()) displayText += " - " + item.published;
      auto row = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, LIST_TOP + (i % PAGE_ITEMS) * ROW_HEIGHT, row.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void RssFeedBrowserActivity::fetchFeed() {
  if (feed.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_FEED_URL);
    requestUpdate();
    return;
  }

  LOG_DBG("RSS", "Fetching: %s", feed.url.c_str());
  RssParser parser;
  if (!HttpDownloader::fetchUrl(
          feed.url, [&parser](const uint8_t* data, size_t len) { return parser.write(data, len) == len; },
          feed.username, feed.password)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }
  parser.flush();

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  feedTitle = parser.getFeedTitle();
  items = std::move(parser).getItems();
  selectorIndex = 0;
  state = items.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
  if (items.empty()) errorMessage = tr(STR_NO_ENTRIES);
  requestUpdate();
}

void RssFeedBrowserActivity::openItem(const RssItem& item) {
  state = BrowserState::ARTICLE_LOADING;
  statusMessage = item.title;
  requestUpdateAndWait();

  RssItem article = item;
  std::string extracted = fetchArticleText(item);
  if (!extracted.empty() && extracted.size() > article.content.size()) {
    article.content = std::move(extracted);
  }

  state = BrowserState::BROWSING;
  startActivityForResult(std::make_unique<RssArticleActivity>(renderer, mappedInput, article),
                         [this](const ActivityResult&) { requestUpdate(); });
}

std::string RssFeedBrowserActivity::fetchArticleText(const RssItem& item) {
  if (!item.link.starts_with("http://") && !item.link.starts_with("https://")) return {};

  auto* fontCache = renderer.getFontCacheManager();
  if (fontCache) fontCache->clearCache();

  auto fetchAndExtract = [&](const std::string& url, std::string& sourceHtml) -> std::string {
    sourceHtml.clear();
    if (!fetchArticleHtml(url, feed.username, feed.password, sourceHtml)) return {};
    return HtmlArticleExtractor::extractReadableText(sourceHtml);
  };

  std::string html;
  std::string extracted = fetchAndExtract(item.link, html);
  if (!extracted.empty()) return extracted;

  std::string ampUrl = extractAmpHtmlUrl(html, item.link);
  if (!ampUrl.empty() && ampUrl != item.link) {
    extracted = fetchAndExtract(ampUrl, html);
    if (!extracted.empty()) return extracted;
  }

  const std::string wikiUrl = wikipediaRenderUrl(item.link);
  if (!wikiUrl.empty() && wikiUrl != item.link && wikiUrl != ampUrl) {
    extracted = fetchAndExtract(wikiUrl, html);
    if (!extracted.empty()) return extracted;
  }

  return {};
}

void RssFeedBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
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
