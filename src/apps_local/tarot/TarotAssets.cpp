#include "TarotAssets.h"

#include <HalStorage.h>

bool TarotAssets::load() {
  if (loaded_) return true;

  const String json = Storage.readFile("/tarot/meanings.json");
  if (json.length() == 0) return false;

  const DeserializationError error = deserializeJson(document_, json);
  loaded_ = !error;
  return loaded_;
}

TarotMeaning TarotAssets::meaning(const int card) const {
  TarotMeaning result;
  if (!loaded_) return result;
  for (JsonObjectConst entry : document_.as<JsonArrayConst>()) {
    if ((entry["id"] | -1) != card) continue;
    result.name = entry["name"] | "Unknown";
    result.meaning = entry["meaning"] | "No meaning found.";
    break;
  }
  return result;
}

std::string TarotAssets::cardPath(const int card) { return "/tarot/cards/" + std::to_string(card) + ".bmp"; }
std::string TarotAssets::thumbPath(const int card) { return "/tarot/thumbs/" + std::to_string(card) + ".bmp"; }
bool TarotAssets::installed() {
  // Menu v3 is the final pure-white 1-bit X4 Pro artwork. Requiring the marker
  // makes older installations enter the downloader once so only menu.png is
  // refreshed; the verified 78 card BMPs remain untouched.
  return Storage.exists("/tarot/cards/0.bmp") && Storage.exists("/tarot/meanings.json") &&
         Storage.exists("/tarot/menu.png") && Storage.exists("/tarot/.menu-v3");
}
