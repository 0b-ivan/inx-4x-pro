#pragma once

#include <string>

class TranslationClient {
 public:
  // Reads /.system/translation.json and calls a LibreTranslate-compatible endpoint.
  // Example: {"endpoint":"http://192.168.1.2:5000/translate","apiKey":"","target":"de"}
  bool translate(const std::string& text, std::string& translated, std::string& error) const;
};

