// Host tests for the fork's German catalogue (src/apps_local/ui/LocalI18n.h).
// The formatted-line patterns are tried in order against one shared `consumed`
// counter; this suite pins the case where a stale counter let "0 / 16" pass
// as "%d POINTS" and every "X / Y" label read "X PUNKTE".

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/ui/LocalI18n.h"
#include "../../src/apps_local/tarot/TarotI18n.h"

static int checks = 0;
static int failures = 0;

static void expect(const char* english, const char* german) {
  ++checks;
  const char* got = toybox::localize(english);
  if (std::strcmp(got, german) != 0) {
    std::printf("FAIL: localize(\"%s\") = \"%s\", want \"%s\"\n", english, got, german);
    ++failures;
  }
}

int main() {
  // Unknown text passes through untouched, including counters.
  expect("0 / 16", "0 / 16");
  expect("1 / 3", "1 / 3");
  expect("16 / 16", "16 / 16");
  expect("12 OF", "12 OF");

  // Each pattern still matches its own line, and only a full line.
  expect("5 POINTS", "5 PUNKTE");
  expect("5 POINTS LEFT", "5 POINTS LEFT");
  expect("3 PLAYED   2 WON", "3 GESPIELT   2 GEWONNEN");
  expect("BEST 7", "BESTE 7");
  expect("MEDALS 3 - 2", "MEDAILLEN 3 - 2");
  expect("2 / 5      1 OF 3 DONE", "2 / 5      1 VON 3 FERTIG");
  expect("12 CLUES   HARDEST CELL PAIR   3 EASY SOLVED", "12 VORGABEN   SCHWERSTE FELDPAAR   3 LEICHT GELOEST");

  expect("STUDY", "ARBEITSZIMMER");
  expect("THEY WIN", "GEGNER GEWINNT");  // first duplicate previously took precedence
  expect("THE CASE", "DER FALL");
  expect("SECOND", "DANACH");
  expect("WEIGHS BOTH WINS", "WAEGT BEIDE SIEGE AB");
  expect("12 MAR 2026", "12 MAER 2026");
  expect("YOU SANK THEIR BATTLESHIP", "DU VERSENKTEST IHR SCHLACHTSCHIFF");
  expect("BATTLESHIP", "SCHIFFE VERSENKEN");
  expect("THE GRID IS YOUR PENCIL", "DAS RASTER IST DEIN NOTIZBLOCK");

  ++checks;
  if (std::strcmp(tarotNameLocalized(0, "The Fool"), "Der Narr") != 0) ++failures;
  ++checks;
  if (std::strcmp(tarotNameLocalized(77, "King of Pentacles"), "König der Münzen") != 0) ++failures;
  ++checks;
  if (std::strcmp(tarotMeaningLocalized(0, "New beginnings"),
                  "Neuanfang, Optimismus, Vertrauen ins Leben und der Mut zu einem Sprung ins Ungewisse.") != 0)
    ++failures;

  // English stays English.
  I18N.language = Language::EN;
  ++checks;
  if (std::strcmp(tarotNameLocalized(0, "The Fool"), "The Fool") != 0) ++failures;
  ++checks;
  if (std::strcmp(tarotMeaningLocalized(0, "New beginnings"), "New beginnings") != 0) ++failures;
  expect("5 POINTS", "5 POINTS");
  expect("STUDY", "STUDY");

  std::printf("locali18n: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
