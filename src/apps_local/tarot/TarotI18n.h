#pragma once

#include <I18n.h>
#include "TarotI18nData.h"

// Tarot card descriptions live on the SD card in English. Keep the asset pack
// language-neutral and localize the displayed meaning in firmware so existing
// installations do not need to download the deck again.
inline const char* tarotMeaningLocalized(const int card, const char* english) {
  if (I18N.getLanguage() != Language::DE) return english;



  constexpr int kMeaningCount = sizeof(tarot_i18n::kGermanMeanings) / sizeof(tarot_i18n::kGermanMeanings[0]);
  return card >= 0 && card < kMeaningCount ? tarot_i18n::kGermanMeanings[card] : english;
}

// Card names stay English in the asset pack (meanings.json); localise them in
// firmware so a German reading shows "Der Narr" rather than "The Fool".
inline const char* tarotNameLocalized(const int card, const char* english) {
  if (I18N.getLanguage() != Language::DE) return english;



  constexpr int kNameCount = sizeof(tarot_i18n::kGermanNames) / sizeof(tarot_i18n::kGermanNames[0]);
  return card >= 0 && card < kNameCount ? tarot_i18n::kGermanNames[card] : english;
}
