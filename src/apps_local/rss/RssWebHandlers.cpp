#include "RssWebHandlers.h"

#include <ArduinoJson.h>
#include <RssParser.h>
#include <WebServer.h>

#include "RssCache.h"
#include "RssFeedStore.h"
#include "RssSync.h"
#include "RssSyncSettings.h"
#include "RssTime.h"
#include "network/HttpDownloader.h"

namespace {
bool readRequest(WebServer& server, JsonDocument& doc) {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Expected a JSON object (maximum 4096 bytes)");
    return false;
  }
  String requestBody = server.arg("plain");
  if (requestBody.length() > 4096 || deserializeJson(doc, requestBody) || !doc.is<JsonObject>()) {
    server.send(400, "text/plain", "Expected a JSON object (maximum 4096 bytes)");
    return false;
  }
  return true;
}

bool readIndex(WebServer& server, const JsonDocument& doc, int& index) {
  index = -1;
  if (doc["index"].isNull()) return true;
  if (!doc["index"].is<int>() || doc["index"].as<int>() < 0 ||
      doc["index"].as<int>() >= static_cast<int>(RSS_STORE.getCount())) {
    server.send(400, "text/plain", "Invalid feed index; reload settings");
    return false;
  }
  index = doc["index"].as<int>();
  return true;
}

bool readFeed(WebServer& server, const JsonDocument& doc, int index, RssFeed& feed) {
  if (!doc["name"].is<const char*>() || !doc["url"].is<const char*>() || !doc["username"].is<const char*>() ||
      (!doc["password"].isNull() && !doc["password"].is<const char*>())) {
    server.send(400, "text/plain", "Name, URL, username and password must be text");
    return false;
  }
  feed.name = doc["name"].as<const char*>();
  feed.url = doc["url"].as<const char*>();
  feed.username = doc["username"].as<const char*>();
  if (index >= 0) feed.password = RSS_STORE.getFeed(index)->password;
  if (!doc["password"].isNull()) feed.password = doc["password"].as<const char*>();
  if (feed.name.size() > 128 || feed.url.size() > 2048 || feed.username.size() > 256 || feed.password.size() > 256) {
    server.send(400, "text/plain", "A field is too long (name 128, URL 2048, credentials 256)");
    return false;
  }
  const size_t scheme = feed.url.find("://");
  const size_t host = scheme == std::string::npos ? 0 : scheme + 3;
  const size_t end = feed.url.find_first_of("/?#", host);
  const std::string authority = feed.url.substr(host, end == std::string::npos ? end : end - host);
  if ((!feed.url.starts_with("https://") && !feed.url.starts_with("http://")) || authority.empty() ||
      authority.find('@') != std::string::npos || feed.url.find_first_of("\r\n\t ") != std::string::npos) {
    server.send(400, "text/plain", "Enter an HTTP(S) feed URL; put credentials in their separate fields");
    return false;
  }
  return true;
}

void listFeeds(WebServer& server) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  const auto& feeds = RSS_STORE.getFeeds();
  for (size_t i = 0; i < feeds.size(); ++i) {
    JsonDocument doc;
    doc["index"] = i;
    doc["name"] = feeds[i].name;
    doc["url"] = feeds[i].url;
    doc["username"] = feeds[i].username;
    doc["hasPassword"] = !feeds[i].password.empty();
    const RssCacheInfo cacheInfo = rsscache::getInfo(feeds[i]);
    doc["cachedItems"] = cacheInfo.itemCount;
    doc["lastSync"] = cacheInfo.lastSyncEpoch;
    // One bounded feed at a time, without exposing its saved password.
    String json;
    serializeJson(doc, json);
    if (i) server.sendContent(",");
    server.sendContent(json);
  }
  server.sendContent("]");
  server.sendContent("");
}

void testFeed(WebServer& server, const RssFeed& feed) {
  RssParser parser;
  size_t received = 0;
  bool tooLarge = false;
  const bool fetched = HttpDownloader::fetchUrl(
      feed.url,
      [&](const uint8_t* data, size_t length) {
        constexpr size_t maxBytes = 256 * 1024;
        if (length > maxBytes - received) {
          tooLarge = true;
          return false;
        }
        received += length;
        parser.write(data, length);
        return !parser.error();
      },
      feed.username, feed.password);
  const int status = HttpDownloader::lastStatus();
  parser.flush();
  if (status == 401) {
    server.send(422, "text/plain", "HTTP 401: login rejected. Check the login username and Nextcloud app password.");
  } else if (status == 403) {
    server.send(422, "text/plain",
                "HTTP 403: access denied. Check feed permissions and any reverse proxy/SSO protection.");
  } else if (status != 200) {
    String error = "Feed request failed (HTTP ";
    error += String(status);
    error += "). Check the URL, Wi-Fi, DNS/TLS and proxy. HTTP 0 means no HTTP response.";
    server.send(422, "text/plain", error);
  } else if (tooLarge) {
    server.send(422, "text/plain", "Feed exceeds the 256 KB connection-test limit. Use a smaller feed.");
  } else if (!parser || (parser.getItems().empty() && parser.getFeedTitle().empty())) {
    server.send(422, "text/plain",
                "HTTP 200, but no usable RSS/Atom feed. A Nextcloud News JSON API or login page is not supported; use "
                "a direct feed URL.");
  } else if (!fetched) {
    server.send(422, "text/plain", "HTTP 200, but the transfer was incomplete. Try again.");
  } else {
    String success = "Connection OK: RSS/Atom feed received, ";
    success += String(static_cast<unsigned long>(parser.getItems().size()));
    success += " entries.";
    server.send(200, "text/plain", success);
  }
}

bool requireIdle(WebServer& server) {
  if (!rsssync::active()) return true;
  server.send(409, "text/plain", "RSS sync is running. Wait or cancel before changing feeds or settings.");
  return false;
}

void saveOrTest(WebServer& server, bool testOnly) {
  if (!requireIdle(server)) return;
  JsonDocument doc;
  int index;
  RssFeed feed;
  if (!readRequest(server, doc) || !readIndex(server, doc, index) || !readFeed(server, doc, index, feed)) return;
  if (testOnly) {
    testFeed(server, feed);
    return;
  }
  if (index < 0 && RSS_STORE.getCount() >= 8) {
    server.send(409, "text/plain", "Maximum of 8 feeds reached");
    return;
  }
  const bool saved = index < 0 ? RSS_STORE.addFeed(feed) : RSS_STORE.updateFeed(index, feed);
  server.send(saved ? 200 : 500, "text/plain", saved ? "Saved" : "Could not save to SD card");
}

void syncStatus(WebServer& server, int status = 200) {
  const auto& progress = rsssync::progress();
  const char* state = "idle";
  switch (progress.state) {
    case rsssync::SyncState::Idle:
      break;
    case rsssync::SyncState::Connecting:
      state = "connecting";
      break;
    case rsssync::SyncState::Feed:
      state = "feed";
      break;
    case rsssync::SyncState::Articles:
      state = "articles";
      break;
    case rsssync::SyncState::Complete:
      state = "complete";
      break;
    case rsssync::SyncState::Incomplete:
      state = "incomplete";
      break;
    case rsssync::SyncState::Cancelled:
      state = "cancelled";
      break;
  }
  JsonDocument doc;
  doc["state"] = state;
  doc["active"] = rsssync::active();
  doc["feedsTotal"] = progress.feedsTotal;
  doc["feedsDone"] = progress.feedsDone;
  doc["feedsFailed"] = progress.feedsFailed;
  doc["articlesTotal"] = progress.articlesTotal;
  doc["articlesSaved"] = progress.articlesSaved;
  doc["articlesFailed"] = progress.articlesFailed;
  doc["failedFeedMask"] = progress.failedFeedMask;
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
}

void syncFeed(WebServer& server) {
  if (!requireIdle(server)) return;
  JsonDocument doc;
  int index;
  if (!readRequest(server, doc) || !readIndex(server, doc, index)) return;
  if (!rsssync::start(index < 0 ? nullptr : RSS_STORE.getFeed(index))) {
    server.send(409, "text/plain", "No feeds to synchronize");
    return;
  }
  syncStatus(server, 202);
}

void syncSettings(WebServer& server, bool save) {
  auto& settings = RSS_SYNC_SETTINGS;
  if (save) {
    if (!requireIdle(server)) return;
    JsonDocument doc;
    int minute;
    if (!readRequest(server, doc)) return;
    if (!doc["enabled"].is<bool>() || !doc["time"].is<const char*>() ||
        !rsstime::parseSyncTime(doc["time"].as<const char*>(), minute)) {
      server.send(400, "text/plain", "Expected enabled and a time in HH:MM format");
      return;
    }
    const bool oldEnabled = settings.enabled;
    const int oldMinute = settings.minute;
    settings.enabled = doc["enabled"].as<bool>();
    settings.minute = minute;
    if ((oldEnabled != settings.enabled || oldMinute != minute) && !settings.saveToFile()) {
      settings.enabled = oldEnabled;
      settings.minute = oldMinute;
      server.send(500, "text/plain", "Could not save schedule to SD card");
      return;
    }
  }
  JsonDocument doc;
  char time[6];
  snprintf(time, sizeof(time), "%02d:%02d", settings.minute / 60, settings.minute % 60);
  doc["enabled"] = settings.enabled;
  doc["time"] = time;
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void deleteFeed(WebServer& server) {
  if (!requireIdle(server)) return;
  JsonDocument doc;
  int index;
  if (!readRequest(server, doc) || !readIndex(server, doc, index)) return;
  if (index < 0) {
    server.send(400, "text/plain", "Missing feed index");
    return;
  }
  const RssFeed removedFeed = *RSS_STORE.getFeed(static_cast<size_t>(index));
  const bool saved = RSS_STORE.removeFeed(index);
  if (saved) rsscache::clearFeed(removedFeed);
  server.send(saved ? 200 : 500, "text/plain", saved ? "Deleted" : "Could not save to SD card");
}
}  // namespace

void registerRssWebRoutes(WebServer& server) {
  RSS_STORE.loadFromFile();
  server.on("/api/rss/schedule", HTTP_GET, [&server] { syncSettings(server, false); });
  server.on("/api/rss/schedule", HTTP_POST, [&server] { syncSettings(server, true); });
  server.on("/api/rss", HTTP_GET, [&server] { listFeeds(server); });
  server.on("/api/rss", HTTP_POST, [&server] { saveOrTest(server, false); });
  server.on("/api/rss/test", HTTP_POST, [&server] { saveOrTest(server, true); });
  server.on("/api/rss/sync", HTTP_GET, [&server] { syncStatus(server); });
  server.on("/api/rss/sync/cancel", HTTP_POST, [&server] {
    rsssync::cancel();
    syncStatus(server);
  });
  server.on("/api/rss/sync", HTTP_POST, [&server] { syncFeed(server); });
  server.on("/api/rss/delete", HTTP_POST, [&server] { deleteFeed(server); });
}
