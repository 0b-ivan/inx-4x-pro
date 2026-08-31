#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>

struct TarotMeaning {
  std::string name = "Unknown";
  std::string meaning = "No meaning found.";
};

class TarotAssets {
 public:
  bool load();
  TarotMeaning meaning(int card) const;
  static std::string cardPath(int card);
  static std::string thumbPath(int card);
  static bool installed();

 private:
  JsonDocument document_;
  bool loaded_ = false;
};
