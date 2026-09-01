#include "network/CalDavClient.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <string>

#include "calendar/CalendarStore.h"

#ifdef SIMULATOR

namespace CalDav {
bool Client::sync(const Config&, const std::string&, const std::string&) {
  Serial.printf("[%lu] [CALDAV] Simulator HTTP sync disabled\n", millis());
  return false;
}
}  // namespace CalDav

#else

#include <base64.h>
#include <esp_http_client.h>

extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

namespace {
constexpr size_t kMaxResponseBytes = 384 * 1024;

struct ResponseContext {
  std::string body;
  bool overflow = false;
};

esp_err_t responseHandler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || !event->data || event->data_len <= 0) return ESP_OK;
  auto* ctx = static_cast<ResponseContext*>(event->user_data);
  if (!ctx || ctx->overflow) return ESP_OK;
  if (ctx->body.size() + static_cast<size_t>(event->data_len) > kMaxResponseBytes) {
    ctx->overflow = true;
    return ESP_FAIL;
  }
  ctx->body.append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
  return ESP_OK;
}

std::string xmlDecode(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size();) {
    if (input.compare(i, 5, "&amp;") == 0) {
      out.push_back('&');
      i += 5;
    } else if (input.compare(i, 4, "&lt;") == 0) {
      out.push_back('<');
      i += 4;
    } else if (input.compare(i, 4, "&gt;") == 0) {
      out.push_back('>');
      i += 4;
    } else if (input.compare(i, 6, "&quot;") == 0) {
      out.push_back('"');
      i += 6;
    } else if (input.compare(i, 6, "&apos;") == 0) {
      out.push_back('\'');
      i += 6;
    } else {
      out.push_back(input[i++]);
    }
  }
  return out;
}

void appendVevents(const std::string& calendarData, std::string& out) {
  size_t pos = 0;
  while (true) {
    const size_t begin = calendarData.find("BEGIN:VEVENT", pos);
    if (begin == std::string::npos) break;
    const size_t endMarker = calendarData.find("END:VEVENT", begin);
    if (endMarker == std::string::npos) break;
    const size_t end = endMarker + std::string("END:VEVENT").size();
    out.append(calendarData, begin, end - begin);
    out.push_back('\n');
    pos = end;
  }
}

std::string extractCalendar(const std::string& xml) {
  std::string merged = "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//Inx X4 Pro//Desk Calendar//EN\n";
  size_t search = 0;
  bool sawCalendarData = false;

  while (true) {
    const size_t name = xml.find("calendar-data", search);
    if (name == std::string::npos) break;

    const size_t tagStart = xml.rfind('<', name);
    if (tagStart == std::string::npos) break;
    if (tagStart + 1 < xml.size() && xml[tagStart + 1] == '/') {
      search = name + 13;
      continue;
    }

    const size_t openEnd = xml.find('>', name);
    if (openEnd == std::string::npos) break;

    const size_t closeTag = xml.find("calendar-data>", openEnd + 1);
    if (closeTag == std::string::npos) break;
    const size_t closeStart = xml.rfind("</", closeTag);
    if (closeStart == std::string::npos || closeStart < openEnd) break;

    sawCalendarData = true;
    std::string data = xml.substr(openEnd + 1, closeStart - openEnd - 1);
    const std::string cdataPrefix = "<![CDATA[";
    const std::string cdataSuffix = "]]>";
    const size_t firstText = data.find_first_not_of(" \t\r\n");
    if (firstText != std::string::npos && data.compare(firstText, cdataPrefix.size(), cdataPrefix) == 0) {
      const size_t cdataEnd = data.rfind(cdataSuffix);
      if (cdataEnd != std::string::npos && cdataEnd >= firstText + cdataPrefix.size()) {
        data = data.substr(firstText + cdataPrefix.size(), cdataEnd - firstText - cdataPrefix.size());
      }
    } else {
      data = xmlDecode(data);
    }

    appendVevents(data, merged);
    search = closeTag + std::string("calendar-data>").size();
  }

  if (!sawCalendarData) return {};
  merged += "END:VCALENDAR\n";
  return merged;
}

}  // namespace

namespace CalDav {

bool Client::sync(const Config& config, const std::string& rangeStartUtc, const std::string& rangeEndUtc) {
  if (config.calendarUrl.rfind("https://", 0) != 0 || config.username.empty() || config.password.empty() ||
      rangeStartUtc.empty() || rangeEndUtc.empty()) {
    Serial.printf("[%lu] [CALDAV] Invalid configuration\n", millis());
    return false;
  }

  const std::string request =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
      "<d:prop><d:getetag/><c:calendar-data><c:expand start=\"" +
      rangeStartUtc + "\" end=\"" + rangeEndUtc +
      "\"/></c:calendar-data></d:prop>"
      "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
      "<c:time-range start=\"" +
      rangeStartUtc + "\" end=\"" + rangeEndUtc +
      "\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";

  ResponseContext response;
  response.body.reserve(16384);

  esp_http_client_config_t cfg = {};
  cfg.url = config.calendarUrl.c_str();
  cfg.event_handler = responseHandler;
  cfg.user_data = &response;
  cfg.timeout_ms = 20000;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.keep_alive_enable = false;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;

  esp_http_client_set_method(client, HTTP_METHOD_REPORT);
  esp_http_client_set_header(client, "Depth", "1");
  esp_http_client_set_header(client, "Content-Type", "application/xml; charset=utf-8");
  esp_http_client_set_header(client, "User-Agent", "Inx-X4Pro-Calendar/1");

  const std::string credentials = config.username + ":" + config.password;
  const String encoded = base64::encode(credentials.c_str());
  const std::string auth = "Basic " + std::string(encoded.c_str());
  esp_http_client_set_header(client, "Authorization", auth.c_str());
  esp_http_client_set_post_field(client, request.c_str(), static_cast<int>(request.size()));

  Serial.printf("[%lu] [CALDAV] Syncing calendar range\n", millis());
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || response.overflow || (status != 207 && status != 200)) {
    Serial.printf("[%lu] [CALDAV] Sync failed: err=%d http=%d overflow=%d\n", millis(), static_cast<int>(err), status,
                  response.overflow ? 1 : 0);
    return false;
  }

  const std::string ics = extractCalendar(response.body);
  if (ics.empty()) {
    Serial.printf("[%lu] [CALDAV] No calendar-data in response\n", millis());
    return false;
  }

  Calendar::CalendarStore store;
  if (!store.save(ics)) {
    Serial.printf("[%lu] [CALDAV] Refusing to replace last-good cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [CALDAV] Calendar cache updated\n", millis());
  return true;
}

}  // namespace CalDav

#endif
