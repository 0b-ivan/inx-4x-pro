#include <HtmlArticleExtractor.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "RssSync.h"
#include "network/HttpDownloader.h"

namespace {
constexpr size_t MAX_ARTICLE_HTML_BYTES = 256 * 1024;
bool startsWithAtCaseInsensitive(const std::string& text, const size_t pos, const char* prefix) {
  const size_t len = strlen(prefix);
  if (pos + len > text.size()) return false;
  for (size_t i = 0; i < len; i++) {
    const char lhs = static_cast<char>(tolower(static_cast<unsigned char>(text[pos + i])));
    const char rhs = static_cast<char>(tolower(static_cast<unsigned char>(prefix[i])));
    if (lhs != rhs) return false;
  }
  return true;
}

size_t findCaseInsensitive(const std::string& text, const char* needle, const size_t start = 0) {
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
      [&](const uint8_t* data, const size_t len) {
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
  return fetched && !truncated && !outHtml.empty();
}

}  // namespace

std::string rsssync::fetchArticleText(const RssFeed& feed, const RssItem& item) {
  if (!item.link.starts_with("http://") && !item.link.starts_with("https://")) return {};

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
