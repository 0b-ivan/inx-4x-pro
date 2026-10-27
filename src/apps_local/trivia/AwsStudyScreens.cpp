#include "AwsStudyScreens.h"
#include "TriviaScreens.h"

#include <cstdio>

namespace triviaui {
namespace {

constexpr int16_t kMargin = 16;

fui::TextStyle style(const fui::FontId font, const fui::TextAlign align,
                     const fui::Color fg = fui::Color::Black) {
  fui::TextStyle s;
  s.font = font;
  s.align = align;
  s.color = fg;
  return s;
}

void label(toybox::Screen& screen, const fui::Rect& rect, const char* txt,
           const bool invert = false) {
  screen.target().text(toybox::inkCentred(rect, toybox::kButtonCut), txt,
                       style(toybox::kSmallFont, fui::TextAlign::Center,
                             invert ? fui::Color::White : fui::Color::Black));
}

void prose(toybox::Screen& screen, const fui::Rect& rect, const char* txt) {
  fui::TextStyle s = style(toybox::kBodyFont, fui::TextAlign::Left);
  const int16_t lineHeight = screen.target().lineHeight(s.font);
  const int lines = lineHeight > 0 ? rect.height / lineHeight : 1;
  s.maxLines = static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
  screen.target().text(rect, txt, s);
}

void chrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
}

int16_t footer(const toybox::Screen& screen) {
  return static_cast<int16_t>(screen.body().bottom() - 96);
}

void button(toybox::Screen& screen, const fui::Rect& rect, const char* txt,
            fui::ActionId action, int16_t value = 0, bool filled = false) {
  if (filled) {
    screen.target().fill(rect, fui::Paint::solid(fui::Color::Black));
  } else {
    screen.target().stroke(rect, fui::Paint::solid(fui::Color::Black), 2);
  }
  label(screen, rect, txt, filled);
  screen.frame().hit(rect, action, value);
}

void footerPair(toybox::Screen& screen, const char* left, fui::ActionId leftAction,
                const char* right, fui::ActionId rightAction) {
  const fui::Rect body = screen.body();
  const int16_t top = static_cast<int16_t>(footer(screen) + 16);
  const int16_t w = static_cast<int16_t>((body.width - kMargin * 3) / 2);
  button(screen, {static_cast<int16_t>(body.x + kMargin), top, w, 62}, left, leftAction);
  button(screen, {static_cast<int16_t>(body.x + kMargin * 2 + w), top, w, 62},
         right, rightAction, 0, true);
}

void footerThree(toybox::Screen& screen, const char* first, fui::ActionId a,
                 const char* second, fui::ActionId b, const char* third, fui::ActionId c) {
  const fui::Rect body = screen.body();
  const int16_t y = static_cast<int16_t>(footer(screen) + 16);
  const int16_t gap = 8;
  const int16_t w = static_cast<int16_t>((body.width - kMargin * 2 - gap * 2) / 3);
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  button(screen, {x, y, w, 62}, first, a);
  button(screen, {static_cast<int16_t>(x + w + gap), y, w, 62}, second, b);
  button(screen, {static_cast<int16_t>(x + 2 * (w + gap)), y, w, 62}, third, c, 0, true);
}

const char* domainName(const int i) {
  constexpr const char* names[] = {
      "1  SDLC AUTOMATION", "2  CONFIGURATION / IAC", "3  RESILIENT CLOUD",
      "4  MONITORING / LOGGING", "5  INCIDENT RESPONSE", "6  SECURITY / COMPLIANCE"};
  return i >= 0 && i < 6 ? names[i] : "";
}

void frameText(toybox::Screen& screen, const AwsPageModel& model, const char* status) {
  const fui::Rect body = screen.body();
  const int16_t top = static_cast<int16_t>(body.y + 20);
  label(screen, {static_cast<int16_t>(body.x + kMargin), top,
                 static_cast<int16_t>(body.width - 2 * kMargin), 32}, status);
  screen.target().fill({static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 36),
                        static_cast<int16_t>(body.width - 2 * kMargin), 2},
                       fui::Paint::solid(fui::Color::Black));
  prose(screen, {static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 54),
                 static_cast<int16_t>(body.width - 2 * kMargin),
                 static_cast<int16_t>(footer(screen) - top - 70)}, model.text);
}

}  // namespace

void buildAwsSummary(toybox::Screen& screen, const AwsSummaryModel& model) {
  chrome(screen, model.german ? "LERNERGEBNIS" : "STUDY RESULTS");
  const fui::Rect body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - 2 * kMargin);
  char score[64];
  std::snprintf(score, sizeof(score),
                model.german ? "%d VON %d RICHTIG" : "%d OF %d CORRECT",
                model.right, model.asked);
  // Results can be swiped without ever moving the navigation buttons.
  const int16_t shift = model.scroll ? 174 : 0;
  if (!model.scroll) label(screen, {x, static_cast<int16_t>(body.y + 74), w, 60}, score);
  char percent[24];
  const int accuracy = model.asked ? 100 * model.right / model.asked : 0;
  std::snprintf(percent, sizeof(percent), "%d%%", accuracy);
  if (!model.scroll) label(screen, {x, static_cast<int16_t>(body.y + 170), w, 64}, percent);
  char progress[72];
  std::snprintf(progress, sizeof(progress),
                model.german ? "%u / %u GESEHEN" : "%u / %u SEEN",
                static_cast<unsigned>(model.seen), static_cast<unsigned>(model.total));
  label(screen, {x, static_cast<int16_t>(body.y + 295 - shift), w, 48}, progress);
  char missed[72];
  std::snprintf(missed, sizeof(missed),
                model.german ? "%u FEHLERFRAGEN OFFEN" : "%u MISSED QUESTIONS",
                static_cast<unsigned>(model.missed));
  label(screen, {x, static_cast<int16_t>(body.y + 365 - shift), w, 48}, missed);
  if (model.scroll) {
    label(screen, {x, static_cast<int16_t>(body.y + 328), w, 55},
          model.german ? "RUNTERWISCHEN: ZURUECK" : "SWIPE DOWN TO GO BACK");
    char hint[80];
    std::snprintf(hint, sizeof(hint),
                  model.german ? "%d ANTWORTEN IN DIESER RUNDE"
                               : "%d ANSWERS IN THIS SESSION", model.asked);
    label(screen, {x, static_cast<int16_t>(body.y + 405), w, 55}, hint);
  }
  if (model.review && model.missed == 0) {
    label(screen, {x, static_cast<int16_t>(body.y + (model.scroll ? 495 : 444)), w, 44},
          model.german ? "FEHLERTRAINING FERTIG" : "REVIEW COMPLETED");
  }
  footerThree(screen, model.german ? "MENUE" : "MENU", ActionAwsBackToMenu,
              model.missed != 0 ? (model.german ? "FEHLER" : "REVIEW")
                                 : (model.german ? "NEU" : "RESTART"),
              model.missed != 0 ? ActionAwsReview : ActionAwsBegin,
              model.german ? "NAECHSTE" : "NEXT", ActionAwsNextQuestion);
}

void buildStudyDeckList(toybox::Screen& screen, const StudyDeckListModel& model) {
  chrome(screen, model.remote
                     ? (model.german ? "GITHUB PAKETE" : "GITHUB PACKS")
                     : (model.german ? "FRAGENPAKETE" : "STUDY PACKS"));
  const fui::Rect body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - kMargin * 2);
  for (uint8_t i = 0; i < model.visible; ++i) {
    const int16_t y = static_cast<int16_t>(body.y + 24 + i * 89);
    const bool selected = model.current == model.indices[i];
    button(screen, {x, y, w, 76}, model.names[i],
           model.remote ? ActionStudyRemoteSelect : ActionStudySelect,
           static_cast<int16_t>(model.indices[i]), selected);
  }
  char status[48];
  std::snprintf(status, sizeof(status), model.german ? "%u PAKETE" : "%u PACKS",
                static_cast<unsigned>(model.total));
  label(screen, {x, static_cast<int16_t>(footer(screen) - 32), w, 25}, status);
  footerThree(screen,
              model.remote ? (model.german ? "LOKAL" : "LOCAL")
                           : (model.german ? "TRIVIA" : "TRIVIA"),
              model.remote ? ActionStudyDecks : ActionAwsHome,
              model.german ? "MEHR" : "MORE",
              model.remote ? ActionStudyRemoteNextPage : ActionStudyNextPage,
              model.remote ? (model.german ? "NEU LADEN" : "REFRESH")
                           : "GITHUB",
              ActionStudyOnline);
}

void buildAwsMenu(toybox::Screen& screen, const AwsMenuModel& model) {
  chrome(screen, model.german ? "LERNPAKET" : "STUDY PACK");
  const fui::Rect body = screen.body();
  char count[64];
  std::snprintf(count, sizeof(count), model.german ? "%u / %u GESEHEN | %u BEREICHE"
                                                      : "%u / %u SEEN | %u DOMAINS",
                static_cast<unsigned>(model.seen), static_cast<unsigned>(model.count),
                static_cast<unsigned>(
                    ((model.enabled >> 0) & 1u) + ((model.enabled >> 1) & 1u) +
                    ((model.enabled >> 2) & 1u) + ((model.enabled >> 3) & 1u) +
                    ((model.enabled >> 4) & 1u) + ((model.enabled >> 5) & 1u)));
  label(screen, {body.x, static_cast<int16_t>(body.y + 8), body.width, 36},
        model.deckName);
  label(screen, {body.x, static_cast<int16_t>(body.y + 50), body.width, 42}, count);
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - 2 * kMargin);
  button(screen, {x, static_cast<int16_t>(body.y + 105), w, 142},
         model.german ? "FRAGEN LERNEN" : "STUDY QUESTIONS",
         ActionAwsBegin, 0, true);
  button(screen, {x, static_cast<int16_t>(body.y + 265), w, 112},
         model.german ? "FEHLERTRAINING" : "REVIEW MISSED",
         ActionAwsReview);
  button(screen, {x, static_cast<int16_t>(body.y + 394), w, 112},
         model.german ? "BEREICHE WAEHLEN" : "SELECT DOMAINS",
         ActionAwsDomains);
  footerPair(screen, model.german ? "PAKETE" : "PACKS", ActionStudyDecks,
             "START", ActionAwsBegin);
}

void buildAwsDomains(toybox::Screen& screen, const AwsDomainsModel& model) {
  chrome(screen, model.german ? "LERNBEREICHE" : "STUDY TOPICS");
  const fui::Rect body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - kMargin * 2);
  for (int i = 0; i < trivia::kAwsDomains; ++i) {
    const int16_t y = static_cast<int16_t>(body.y + 12 + i * 89);
    const fui::Rect box{x, y, w, 77};
    const bool on = (model.enabled & (1u << i)) != 0;
    screen.target().stroke(box, fui::Paint::solid(fui::Color::Black), 2);
    if (on) {
      const fui::Rect tick{static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 25), 27, 27};
      screen.target().fill(tick, fui::Paint::solid(fui::Color::Black));
    }
    const fui::Rect textBox{static_cast<int16_t>(x + 41), y,
                            static_cast<int16_t>(w - 42), 77};
    screen.target().text(toybox::inkCentred(textBox, toybox::kButtonCut),
                         model.topics[i] ? model.topics[i] : domainName(i),
                         style(toybox::kSmallFont, fui::TextAlign::Left));
    screen.frame().hit(box, ActionAwsToggleDomain, static_cast<int16_t>(i));
  }
  footerPair(screen, model.german ? "ALLE AN" : "ALL ON", ActionAwsAllDomains,
             model.german ? "FERTIG" : "DONE", ActionAwsBackToMenu);
}

void buildAwsQuestion(toybox::Screen& screen, const AwsPageModel& model) {
  chrome(screen, model.german ? "LERNFRAGE" : "STUDY QUESTION");
  char status[44];
  std::snprintf(status, sizeof(status), model.german ? "BEREICH %u | FRAGE LESEN"
                                                      : "TOPIC %u | READ THE QUESTION",
                static_cast<unsigned>(model.domain));
  frameText(screen, model, status);
  footerPair(screen, model.german ? "ENDE" : "EXIT", ActionAwsFinish,
             model.more ? (model.german ? "MEHR TEXT" : "MORE TEXT")
                        : (model.german ? "ANTWORTEN" : "ANSWERS"), ActionAwsPageNext);
  if (model.selected) {
    const auto body = screen.body();
    button(screen, {static_cast<int16_t>(body.x + body.width - 206),
                    static_cast<int16_t>(footer(screen) - 50), 190, 43},
           model.german ? "JETZT PRUEFEN" : "GRADE NOW", ActionAwsSubmit);
  }
}

void buildAwsOption(toybox::Screen& screen, const AwsPageModel& model) {
  chrome(screen, model.german ? "ANTWORTEN" : "ANSWERS");
  char status[64];
  const bool marked = (model.selected & (1u << model.option)) != 0;
  std::snprintf(status, sizeof(status), model.german ? "ANTWORT %c/%u | %s"
                                                      : "OPTION %c / %u | %s",
                static_cast<char>('A' + model.option),
                static_cast<unsigned>(model.optionCount),
                marked ? (model.german ? "GEWAEHLT" : "SELECTED")
                       : (model.german ? "OFFEN" : "NOT SELECTED"));
  frameText(screen, model, status);
  // Three always-visible controls: select/deselect, previous, and next text/
  // next option or grade. Multi-answer questions must be submitted explicitly.
  const bool lastPage = !model.more && model.option + 1 == model.optionCount;
  const bool needsSelection = lastPage && model.selected == 0;
  footerThree(screen,
              marked ? (model.german ? "ABWAEHLEN" : "UNMARK")
                     : (model.german ? "WAEHLEN" : "MARK"), ActionAwsToggleOption,
              model.german ? "ZURUECK" : "BACK", ActionAwsPageBack,
              model.more ? (model.german ? "MEHR" : "MORE")
                         : !lastPage ? (model.german ? "WEITER" : "NEXT")
                         : needsSelection ? (model.german ? "WAEHLEN" : "SELECT")
                                          : (model.german ? "PRUEFEN" : "GRADE"),
              needsSelection ? ActionAwsToggleOption :
              lastPage ? ActionAwsSubmit : ActionAwsPageNext);
  if (model.selected) {
    const auto body = screen.body();
    button(screen, {static_cast<int16_t>(body.x + body.width - 206),
                    static_cast<int16_t>(footer(screen) - 50), 190, 43},
           model.german ? "JETZT PRUEFEN" : "GRADE NOW", ActionAwsSubmit);
  }
}

void buildAwsExplanation(toybox::Screen& screen, const AwsPageModel& model) {
  chrome(screen, model.german ? "LOESUNG" : "REVIEW");
  char status[80];
  char key[8] = {};
  char chosen[8] = {};
  int k = 0, p = 0;
  for (int i = 0; i < model.optionCount; ++i) {
    if (model.correct & (1u << i)) key[k++] = static_cast<char>('A' + i);
    if (model.selected & (1u << i)) chosen[p++] = static_cast<char>('A' + i);
  }
  std::snprintf(status, sizeof(status), model.german ? "%s %d/%d | DU %s SOLL %s"
                                                      : "%s %d/%d | YOU %s KEY %s",
                model.correctAnswer ? (model.german ? "RICHTIG" : "RIGHT")
                                    : (model.german ? "FALSCH" : "WRONG"),
                model.right, model.asked, chosen, key);
  frameText(screen, model, status);
  footerPair(screen, model.german ? "ENDE" : "END", ActionAwsFinish,
             model.more ? (model.german ? "MEHR TEXT" : "MORE TEXT")
                        : (model.german ? "NAECHSTE" : "NEXT QUESTION"), ActionAwsPageNext);
  if (model.more) {
    const auto body = screen.body();
    button(screen, {static_cast<int16_t>(body.x + body.width - 206),
                    static_cast<int16_t>(footer(screen) - 50), 190, 43},
           model.german ? "NAECHSTE FRAGE" : "NEXT QUESTION", ActionAwsNextQuestion);
  }
}

void buildAwsGlossary(toybox::Screen& screen, const AwsGlossaryModel& model) {
  chrome(screen, model.german ? "AWS GLOSSAR" : "AWS GLOSSARY");
  const auto body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - 2 * kMargin);
  if (model.description) {
    label(screen, {x, static_cast<int16_t>(body.y + 18), w, 52},
          model.term ? model.term : "AWS");
    prose(screen, {x, static_cast<int16_t>(body.y + 90), w, 415},
          model.description);
  } else {
    label(screen, {x, static_cast<int16_t>(body.y + 14), w, 40},
          model.german ? "ERKANNTE BEGRIFFE" : "RECOGNIZED TERMS");
    for (int i = 0; i < model.count && i < 6; ++i) {
      button(screen, {x, static_cast<int16_t>(body.y + 70 + i * 79), w, 65},
             model.terms[i], ActionAwsGlossaryTerm, static_cast<int16_t>(i));
    }
  }
  footerPair(screen, model.german ? "ZURUECK" : "BACK", ActionAwsGlossaryBack,
             model.german ? "WEITER LERNEN" : "RESUME", ActionAwsGlossaryBack);
}

}  // namespace triviaui
