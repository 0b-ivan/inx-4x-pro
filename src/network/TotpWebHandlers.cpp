#include "TotpWebHandlers.h"

#include <ArduinoJson.h>
#include <WebServer.h>

#include <array>

#include "apps_local/totp/TotpWebStore.h"
#include "html/AuthenticatorPageHtml.generated.h"

namespace {
unsigned long blockedUntil = 0;
uint8_t failedPins = 0;

void sendResult(WebServer& server, totpweb::Result result) {
  int status = 400;
  if (result == totpweb::Result::BadPin) {
    if (++failedPins >= 5) {
      blockedUntil = millis() + 30000UL;
      failedPins = 0;
    }
    status = 401;
  } else if (result == totpweb::Result::NoVault) {
    status = 409;
  } else if (result == totpweb::Result::Full) {
    status = 409;
  } else if (result == totpweb::Result::NotFound) {
    status = 404;
  } else if (result == totpweb::Result::StorageError) {
    status = 500;
  } else if (result == totpweb::Result::Unsupported) {
    status = 501;
  }
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = totpweb::resultMessage(result);
  String body;
  serializeJson(doc, body);
  server.send(status, "application/json", body);
}

bool rateLimited(WebServer& server) {
  if (blockedUntil == 0 || static_cast<long>(millis() - blockedUntil) >= 0) {
    blockedUntil = 0;
    return false;
  }
  server.send(429, "application/json", "{\"ok\":false,\"error\":\"Too many invalid PIN attempts; retry shortly\"}");
  return true;
}

bool parseBody(WebServer& server, JsonDocument& doc) {
  if (!server.hasArg("plain") || deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON body\"}");
    return false;
  }
  return true;
}

const char* pinFrom(JsonDocument& doc) {
  return doc["pin"].is<const char*>() ? doc["pin"].as<const char*>() : nullptr;
}

void handleList(WebServer& server) {
  if (rateLimited(server)) return;
  JsonDocument request;
  if (!parseBody(server, request)) return;
  std::array<totpweb::AccountMeta, totpweb::kMaxAccounts> accounts{};
  size_t count = 0;
  const auto result = totpweb::list(pinFrom(request), accounts.data(), accounts.size(), count);
  if (result != totpweb::Result::Ok) return sendResult(server, result);
  failedPins = 0;

  JsonDocument response;
  response["ok"] = true;
  JsonArray items = response["accounts"].to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    JsonObject item = items.add<JsonObject>();
    item["index"] = i;
    item["name"] = accounts[i].name;
    item["digits"] = accounts[i].digits;
    item["period"] = accounts[i].period;
  }
  String body;
  serializeJson(response, body);
  server.send(200, "application/json", body);
}

void handleImport(WebServer& server) {
  if (rateLimited(server)) return;
  JsonDocument request;
  if (!parseBody(server, request)) return;
  const char* pin = pinFrom(request);
  totpweb::Result result = totpweb::Result::Invalid;
  if (request["uri"].is<const char*>()) {
    result = totpweb::importUri(pin, request["uri"].as<const char*>());
  } else if (request["name"].is<const char*>() && request["secret"].is<const char*>()) {
    const uint8_t digits = request["digits"] | 6;
    const uint16_t period = request["period"] | 30;
    result = totpweb::add(pin, request["name"].as<const char*>(), request["secret"].as<const char*>(), digits, period);
  }
  if (result != totpweb::Result::Ok) return sendResult(server, result);
  failedPins = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleDelete(WebServer& server) {
  if (rateLimited(server)) return;
  JsonDocument request;
  if (!parseBody(server, request)) return;
  if (!request["index"].is<size_t>()) return sendResult(server, totpweb::Result::Invalid);
  const auto result = totpweb::remove(pinFrom(request), request["index"].as<size_t>());
  if (result != totpweb::Result::Ok) return sendResult(server, result);
  failedPins = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}
}  // namespace

void registerTotpWebRoutes(WebServer& server) {
  server.on("/authenticator", HTTP_GET, [&server] {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", AuthenticatorPageHtml, sizeof(AuthenticatorPageHtml));
  });
  server.on("/api/totp/list", HTTP_POST, [&server] { handleList(server); });
  server.on("/api/totp/import", HTTP_POST, [&server] { handleImport(server); });
  server.on("/api/totp/delete", HTTP_POST, [&server] { handleDelete(server); });
}
