#include "TranslationClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SDCardManager.h>
#include <WiFi.h>

bool TranslationClient::translate(const std::string& text, std::string& translated, std::string& error) const {
  translated.clear();
  error.clear();
  if (text.empty()) {
    error = "No text selected.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected.";
    return false;
  }

  JsonDocument config;
  FsFile file = SdMan.open("/.system/translation.json", O_READ);
  if (!file || deserializeJson(config, file)) {
    if (file) file.close();
    error = "Create /.system/translation.json first.";
    return false;
  }
  file.close();
  const std::string endpoint = config["endpoint"] | "";
  const std::string apiKey = config["apiKey"] | "";
  const std::string target = config["target"] | "de";
  const std::string source = config["source"] | "auto";
  if (endpoint.empty()) {
    error = "Translation endpoint is missing.";
    return false;
  }

  JsonDocument request;
  request["q"] = text;
  request["source"] = source;
  request["target"] = target;
  request["format"] = "text";
  if (!apiKey.empty()) request["api_key"] = apiKey;
  std::string body;
  serializeJson(request, body);

  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(endpoint.c_str())) {
    error = "Could not open translation server.";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(String(body.c_str()));
  const String response = http.getString();
  http.end();
  if (status < 200 || status >= 300) {
    error = "Translation server returned HTTP " + std::to_string(status) + ".";
    return false;
  }
  JsonDocument result;
  if (deserializeJson(result, response)) {
    error = "Invalid translation response.";
    return false;
  }
  translated = result["translatedText"] | "";
  if (translated.empty()) {
    error = result["error"] | "Translation returned no text.";
    return false;
  }
  return true;
}
