#pragma once

#if defined(__has_include)
#if __has_include(<I18n.h>)
#include <I18n.h>
#define TOYBOX_LOCAL_I18N_HAS_CORE 1
#else
#define TOYBOX_LOCAL_I18N_HAS_CORE 0
#endif
#else
#define TOYBOX_LOCAL_I18N_HAS_CORE 0
#endif

#include <cstdio>
#include "LocalI18nFormats.h"
#include <cstring>

namespace toybox {

// Local app strings are authored in lib/I18n/translations/{english,german}.yaml
// and generated into LocalI18nData.inc. This compact German-only lookup avoids
// overflowing the 15-bit core translation offsets and the 32-language tables.
struct LocalTranslation {
  const char* english;
  const char* german;
};

inline bool isGermanLanguage() {
#if TOYBOX_LOCAL_I18N_HAS_CORE
  return I18N.getLanguage() == Language::DE;
#else
  return false;
#endif
}

inline const char* localize(const char* text) {
  if (text == nullptr) return text;
#if TOYBOX_LOCAL_I18N_HAS_CORE
  if (!isGermanLanguage()) return text;
#else
  return text;
#endif

  // Generated from the central English/German YAML files by gen_i18n.py.
  static constexpr LocalTranslation kGerman[] = {
#include "LocalI18nData.inc"
  };

  for (const auto& entry : kGerman) {
    if (std::strcmp(text, entry.english) == 0) return entry.german;
  }

  // Rendering is serialized by ActivityManager's render lock. One shared
  // buffer therefore avoids both a large per-frame stack object and a fallible
  // heap allocation while keeping formatted status lines local to this fork.
  static char formatted[128];
  int first = 0;
  int second = 0;
  int third = 0;
  int consumed = 0;
  char word[32] = {};
  char other[32] = {};
  // Every pattern below tests text[consumed] for a full match. %n only writes
  // when the whole pattern matched, so a stale value from an earlier attempt
  // would accept a partial one ("0 / 16" as "%d POINTS").
  const auto scan = [text, &consumed](const char* format, auto*... out) {
    consumed = 0;
    return std::sscanf(text, format, out...);
  };
  const auto germanShip = [](const char* ship) {
    if (std::strcmp(ship, "CARRIER") == 0) return "TRAEGER";
    if (std::strcmp(ship, "BATTLESHIP") == 0) return "SCHLACHTSCHIFF";
    if (std::strcmp(ship, "CRUISER") == 0) return "KREUZER";
    if (std::strcmp(ship, "SUBMARINE") == 0) return "U-BOOT";
    if (std::strcmp(ship, "DESTROYER") == 0) return "ZERSTOERER";
    return ship;
  };
  const auto germanSudokuTerm = [](const char* term) {
    static constexpr LocalTranslation kTerms[] = {
        {"EASY", "LEICHT"},
        {"MEDIUM", "MITTEL"},
        {"HARD", "SCHWER"},
        {"EXPERT", "EXPERTE"},
        {"NOTHING", "NICHTS"},
        {"ONLY DIGIT", "EINZIGE ZAHL"},
        {"ONLY CELL", "EINZIGES FELD"},
        {"LOCKED", "GESPERRT"},
        {"CELL PAIR", "FELDPAAR"},
        {"DIGIT PAIR", "ZAHLENPAAR"},
        {"CELL TRIO", "FELDTRIO"},
        {"DIGIT TRIO", "ZAHLENTRIO"},
        {"RECTANGLE", "RECHTECK"},
        {"Y CHAIN", "Y-KETTE"},
        {"SWORDFISH", "SCHWERTFISCH"},
    };
    for (const auto& entry : kTerms) {
      if (std::strcmp(term, entry.english) == 0) return entry.german;
    }
    return term;
  };
  const auto germanMonth = [](const char* month) -> const char* {
    static constexpr LocalTranslation kMonths[] = {
        {"JAN", "JAN"}, {"FEB", "FEB"}, {"MAR", "MAER"}, {"APR", "APR"}, {"MAY", "MAI"}, {"JUN", "JUN"},
        {"JUL", "JUL"}, {"AUG", "AUG"}, {"SEP", "SEP"},  {"OCT", "OKT"}, {"NOV", "NOV"}, {"DEC", "DEZ"},
    };
    for (const auto& entry : kMonths) {
      if (std::strcmp(month, entry.english) == 0) return entry.german;
    }
    return nullptr;
  };

  if (scan("%d PLAYED   %d WON   STREAK %d%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_PLAYED_D_WON_STREAK_D), first, second, third);
    return formatted;
  }
  if (scan("%d PLAYED   %d WON%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_PLAYED_D_WON), first, second);
    return formatted;
  }
  if (scan("BEST %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_BEST_D), first);
    return formatted;
  }
  if (scan("%d SECONDS%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_SECONDS), first);
    return formatted;
  }
  if (scan("%d LISTS%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_LISTS), first);
    return formatted;
  }
  if (scan("OUT OF %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_OUT_OF_D), first);
    return formatted;
  }
  if (scan("TOTAL  %d %31s  %d %31s%n", &first, word, &second, other, &consumed) == 4 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_TOTAL_D_S_D_S), first, first == 1 ? "RUNDE" : "RUNDEN", second,
                  second == 1 ? "WORT" : "WOERTER");
    return formatted;
  }
  if (scan("MEDALS %d - %d%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_MEDALS_D_D), first, second);
    return formatted;
  }
  if (scan("%d-%d OF %d%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_D_OF_D), first, second, third);
    return formatted;
  }
  if (scan("%d MOVES. THE DECK NEVER STOOD A CHANCE.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MOVES_THE_DECK_NEVER_STOOD_A_CHANCE), first);
    return formatted;
  }
  if (scan("%d MOVES, AND NOT ONE OF THEM WASTED.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MOVES_AND_NOT_ONE_OF_THEM_WASTED), first);
    return formatted;
  }
  if (scan("%d MOVES. SOMEBODY HAS DONE THIS BEFORE.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MOVES_SOMEBODY_HAS_DONE_THIS_BEFORE), first);
    return formatted;
  }
  if (scan("%d MOVES. THE STOCK IS STILL RECOVERING.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MOVES_THE_STOCK_IS_STILL_RECOVERING), first);
    return formatted;
  }
  if (scan("%d CLEARED, %d IN A ROW%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_CLEARED_D_IN_A_ROW), first, second);
    return formatted;
  }
  if (scan("%d MOVES DEEP%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MOVES_DEEP), first);
    return formatted;
  }
  if (scan("%d DEALT, %d CLEARED, %d IN A ROW%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_DEALT_D_CLEARED_D_IN_A_ROW), first, second, third);
    return formatted;
  }
  if (scan("%d PLAYED   %d CLEARED%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_PLAYED_D_CLEARED), first, second);
    return formatted;
  }
  if (scan("%d OF %d CELLS OPENED%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D_CELLS_OPENED), first, second);
    return formatted;
  }
  if (scan("%d OF %d MINES FLAGGED%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D_MINES_FLAGGED), first, second);
    return formatted;
  }
  if (scan("%31s, STARTING FRESH%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_STARTING_FRESH), germanSudokuTerm(word));
    return formatted;
  }
  if (scan("%d SOLVED   BEST %31s%n", &first, word, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_SOLVED_BEST_S), first, word);
    return formatted;
  }
  if (scan("%d SOLVED   NO %31s TIME YET%n", &first, word, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_SOLVED_NO_S_TIME_YET), first, germanSudokuTerm(word));
    return formatted;
  }
  if (scan("%d HINT, SO NO TIME RECORDED%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_HINT_SO_NO_TIME_RECORDED), first);
    return formatted;
  }
  if (scan("%d HINTS, SO NO TIME RECORDED%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_HINTS_SO_NO_TIME_RECORDED), first);
    return formatted;
  }
  if (scan("YOUR BEST %31s YET%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOUR_BEST_S_YET), germanSudokuTerm(word));
    return formatted;
  }
  if (scan("YOUR BEST IS %31s%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOUR_BEST_IS_S), word);
    return formatted;
  }
  int techniqueAt = 0;
  if (scan("%d CLUES   HARDEST %n", &first, &techniqueAt) == 1 && techniqueAt > 0) {
    const char* separator = std::strstr(text + techniqueAt, "   ");
    const size_t techniqueLength = separator == nullptr ? 0 : static_cast<size_t>(separator - (text + techniqueAt));
    int detailConsumed = 0;
    if (techniqueLength > 0 && techniqueLength < sizeof(word) &&
        std::sscanf(separator + 3, "%d %31s SOLVED%n", &second, other, &detailConsumed) == 2 &&
        separator[3 + detailConsumed] == '\0') {
      std::memcpy(word, text + techniqueAt, techniqueLength);
      word[techniqueLength] = '\0';
      std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_CLUES_HARDEST_S_D_S_SOLVED), first,
                    germanSudokuTerm(word), second, germanSudokuTerm(other));
      return formatted;
    }
  }
  if (scan("%d W   %d L   %d D%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_W_D_L_D_D), first, second, third);
    return formatted;
  }
  if (scan("%d WHITE%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_WHITE), first);
    return formatted;
  }
  if (scan("%d BLACK%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_BLACK), first);
    return formatted;
  }
  if (scan("%d OVER%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OVER), first);
    return formatted;
  }
  const size_t textLength = std::strlen(text);
  static constexpr char kMoveSuffix[] = "'S MOVE";
  constexpr size_t kMoveSuffixLength = sizeof(kMoveSuffix) - 1;
  if (textLength > kMoveSuffixLength && std::strcmp(text + textLength - kMoveSuffixLength, kMoveSuffix) == 0) {
    const size_t nameLength = textLength - kMoveSuffixLength;
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_S_MOVE), static_cast<int>(nameLength), text);
    return formatted;
  }
  if (scan("%d OF %d%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D), first, second);
    return formatted;
  }
  if (scan("%d-%d OF %d%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_D_OF_D_2), first, second, third);
    return formatted;
  }
  if (scan("REACHED %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_REACHED_D), first);
    return formatted;
  }
  if (scan("%d PLAYED%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_PLAYED), first);
    return formatted;
  }
  if (scan("%d QUESTION ON THE CARD%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_QUESTION_ON_THE_CARD), first);
    return formatted;
  }
  if (scan("%d QUESTIONS ON THE CARD%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_QUESTIONS_ON_THE_CARD), first);
    return formatted;
  }
  if (scan("%d OF %d SEEN%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D_SEEN), first, second);
    return formatted;
  }
  if (scan("Level %d of %d%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_LEVEL_D_OF_D), first, second);
    return formatted;
  }
  if (scan("The questions need about %d MB free and the card has %d MB. Delete something from the card, then try "
           "again. Nothing was written.%n",
           &first, &second, &consumed) == 2 &&
      text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_THE_QUESTIONS_NEED_ABOUT_D_MB_FREE_AND_THE_CARD_HA), first, second);
    return formatted;
  }
  if (scan("Fetching the questions: %d of %d MB. Back stops it.%n", &first, &second, &consumed) == 2 &&
      text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_FETCHING_THE_QUESTIONS_D_OF_D_MB_BACK_STOPS_IT), first,
                  second);
    return formatted;
  }
  if (scan("Fetching the questions: %d MB so far. Back stops it.%n", &first, &consumed) == 1 &&
      text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_FETCHING_THE_QUESTIONS_D_MB_SO_FAR_BACK_STOPS_IT), first);
    return formatted;
  }
  if (scan("%d questions on the card. Ready when you are.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_QUESTIONS_ON_THE_CARD_READY_WHEN_YOU_ARE), first);
    return formatted;
  }
  if (scan("You got %d of %d.%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOU_GOT_D_OF_D), first, second);
    return formatted;
  }
  if (scan("That question will not come back. %d hidden so far.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_THAT_QUESTION_WILL_NOT_COME_BACK_D_HIDDEN_SO_FAR), first);
    return formatted;
  }
  if (scan("ROUND %d   %d POINT%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_ROUND_D_D_POINT), first, second);
    return formatted;
  }
  if (scan("ROUND %d   %d POINTS%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_ROUND_D_D_POINTS), first, second);
    return formatted;
  }
  if (scan("%d ROUND ABANDONED SO FAR%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_ROUND_ABANDONED_SO_FAR), first);
    return formatted;
  }
  if (scan("%d ROUNDS ABANDONED SO FAR%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_ROUNDS_ABANDONED_SO_FAR), first);
    return formatted;
  }
  if (scan("YOU LOCKED %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOU_LOCKED_D), first);
    return formatted;
  }
  if (scan("GUESS  %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_GUESS_D), first);
    return formatted;
  }
  if (scan("NUMBER %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_NUMBER_D), first);
    return formatted;
  }
  if (scan("PLAY ROUND %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_PLAY_ROUND_D), first);
    return formatted;
  }
  if (scan("%d POINT IN %d ROUND%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_POINT_IN_D_ROUND), first, second);
    return formatted;
  }
  if (scan("%d POINTS IN %d ROUNDS%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_POINTS_IN_D_ROUNDS), first, second);
    return formatted;
  }
  {
    int fourth = 0;
    if (scan("%d / %d      %d OF %d DONE%n", &first, &second, &third, &fourth, &consumed) == 4 &&
        text[consumed] == '\0') {
      std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_D_D_OF_D_DONE), first, second, third, fourth);
      return formatted;
    }
  }
  if (scan("%d OF %d CLUES USED%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D_CLUES_USED), first, second);
    return formatted;
  }
  if (scan("%d OF %d SQUARES SETTLED%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_OF_D_SQUARES_SETTLED), first, second);
    return formatted;
  }
  if (scan("SOLVED, AFTER %d WRONG ACCUSATIONS.%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_SOLVED_AFTER_D_WRONG_ACCUSATIONS), first);
    return formatted;
  }
  if (scan("%d PLAYED   %d PERFECT   STREAK %d%n", &first, &second, &third, &consumed) == 3 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_PLAYED_D_PERFECT_STREAK_D), first, second, third);
    return formatted;
  }
  if (scan("%d %31s %d%n", &first, word, &second, &consumed) == 3 && text[consumed] == '\0') {
    const char* month = germanMonth(word);
    if (month != nullptr) {
      std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_S_D), first, month, second);
      return formatted;
    }
  }
  if (scan("ROUND %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_ROUND_D), first);
    return formatted;
  }
  if (scan("%d POINTS%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_POINTS), first);
    return formatted;
  }
  if (scan("%d POINT%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_POINT), first);
    return formatted;
  }
  if (scan("%d YAHTZEES ROLLED%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_YAHTZEES_ROLLED), first);
    return formatted;
  }
  if (scan("ROLL AGAIN (%d LEFT)%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_ROLL_AGAIN_D_LEFT), first);
    return formatted;
  }
  if (scan("%d MORE%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_MORE), first);
    return formatted;
  }
  if (scan("TOP HALF BONUS  %d - %d%n", &first, &second, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_TOP_HALF_BONUS_D_D), first, second);
    return formatted;
  }
  if (scan("%d LEFT%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_D_LEFT), first);
    return formatted;
  }
  if (scan("LAST GAME: WON BY %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_LAST_GAME_WON_BY_D), first);
    return formatted;
  }
  if (scan("LAST GAME: LOST BY %d%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_LAST_GAME_LOST_BY_D), first);
    return formatted;
  }
  if (scan("%31s WINS%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_WINS), word);
    return formatted;
  }
  if (scan("FIRE AT %31s%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_FIRE_AT_S), word);
    return formatted;
  }
  if (scan("WAITING FOR %31s%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_WAITING_FOR_S), word);
    return formatted;
  }
  if (scan("%31s: HIT%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_HIT), word);
    return formatted;
  }
  if (scan("%31s: MISS%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_MISS), word);
    return formatted;
  }
  if (scan("%31s HIT YOU AT %31s%n", word, other, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_HIT_YOU_AT_S), word, other);
    return formatted;
  }
  if (scan("%31s MISSED AT %31s%n", word, other, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_MISSED_AT_S), word, other);
    return formatted;
  }
  if (scan("%31s SANK YOUR %31s%n", word, other, &consumed) == 2 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_S_SANK_YOUR_S), word, germanShip(other));
    return formatted;
  }
  if (scan("YOU SANK THEIR %31s%n", word, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOU_SANK_THEIR_S), germanShip(word));
    return formatted;
  }
  if (scan("YOU WIN IN %d SHOTS%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_YOU_WIN_IN_D_SHOTS), first);
    return formatted;
  }
  if (scan("THEY WIN IN %d SHOTS%n", &first, &consumed) == 1 && text[consumed] == '\0') {
    std::snprintf(formatted, sizeof(formatted), localizedFormat(LocalFormat::LOCAL_FORMAT_THEY_WIN_IN_D_SHOTS), first);
    return formatted;
  }
  return text;
}

}  // namespace toybox
