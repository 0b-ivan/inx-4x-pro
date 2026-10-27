#include "AwsStudyScreens.h"
#include "TriviaScreens.h"
#include "AwsGlossary.h"

#include <cstdio>
#include <cstring>
#include <cctype>

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


bool termAt(const char* line, const size_t pos, const char* term) {
  if (!line || !term || (pos > 0 &&
      trivia::awsAsciiWordChar(static_cast<unsigned char>(line[pos - 1])))) return false;
  size_t i = 0;
  while (term[i] && line[pos + i] &&
         std::tolower(static_cast<unsigned char>(line[pos + i])) ==
         std::tolower(static_cast<unsigned char>(term[i]))) ++i;
  return term[i] == '\0' &&
         !trivia::awsAsciiWordChar(static_cast<unsigned char>(line[pos + i]));
}

void dottedTerms(toybox::Screen& screen, const fui::Rect& text, const fui::TextStyle& style,
                 const AwsPageModel& model) {
  if (!model.termHitCount || !model.termHits) return;
  *model.termHitCount = 0;
  const auto& target = screen.target();
  // Work from the SDK's actual wrapped glyph runs, not byte columns: this
  // keeps underlines and taps aligned with proportional fonts and UTF-8.
  fui::layoutText(target, text, model.text, style, [&](const char* run, fui::Rect line) {
    const size_t len = std::strlen(run);
    for (size_t pos = 0; pos < len; ++pos) {
      for (uint8_t i = 0; i < sizeof(trivia::kAwsGlossary) /
                                    sizeof(trivia::kAwsGlossary[0]); ++i) {
        if (*model.termHitCount >= kAwsInlineTermHits) return;
        const auto& entry = trivia::kAwsGlossary[i];
        if (!termAt(run, pos, entry.match)) continue;
        const size_t termLen = std::strlen(entry.match);
        char prefix[224] = {};
        if (pos >= sizeof(prefix) || pos + termLen > len) continue;
        std::memcpy(prefix, run, pos);
        const int16_t px = target.measureText(style.font, prefix, style).width;
        const int16_t pw = target.measureText(style.font, entry.match, style).width;
        if (pw <= 0) continue;
        const int16_t x = static_cast<int16_t>(line.x + px);
        if (x < text.x || x >= text.right()) continue;
        const int16_t w = static_cast<int16_t>(x + pw > text.right() ?
                                                text.right() - x : pw);
        if (w < 4) continue;
        // Dotted baseline offers a standard reader-style hint without
        // altering the underlying question text or font rasterization.
        const int16_t y = static_cast<int16_t>(line.bottom() - 2);
        for (int16_t dot = 0; dot < w; dot += 5)
          screen.target().fill({static_cast<int16_t>(x + dot), y, 2, 1},
                               fui::Paint::solid(fui::Color::Black));
        const fui::Rect hit{x, line.y, w, line.height};
        model.termHits[*model.termHitCount] = AwsTermHit{hit, i};
        ++*model.termHitCount;
        if (!model.glossaryOverlay)
          screen.frame().hit(hit, ActionAwsGlossaryTerm, i);
        pos += termLen - 1;
        break;
      }
    }
  });
}

void frameText(toybox::Screen& screen, const AwsPageModel& model, const char* status) {
  const fui::Rect body = screen.body();
  const int16_t top = static_cast<int16_t>(body.y + 20);
  label(screen, {static_cast<int16_t>(body.x + kMargin), top,
                 static_cast<int16_t>(body.width - 2 * kMargin), 32}, status);
  screen.target().fill({static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 36),
                        static_cast<int16_t>(body.width - 2 * kMargin), 2},
                       fui::Paint::solid(fui::Color::Black));
  // Top-aligned: layoutText centres its block, so the rect is shrunk to the
  // wrapped height. A short last page then continues where the reader's eye is.
  fui::Rect text = awsTextViewport(body);
  const uint8_t lines = awsTextLines(screen.target(), toybox::kBodyFont, text.height);
  fui::TextStyle s = style(toybox::kBodyFont, fui::TextAlign::Left);
  s.maxLines = lines;
  const int16_t used = fui::measureWrappedText(screen.target(), model.text, s, text.width).height;
  if (used > 0 && used < text.height) text.height = used;
  screen.target().text(text, model.text, s);
  dottedTerms(screen, text, s, model);
  // E-ink-friendly reader scrollbar: page position is stable and the rail is
  // tappable, with six jump targets instead of continuous redraw while moving.
  const fui::Rect rail{static_cast<int16_t>(text.x + text.width + 5),
                       text.y, 11, awsTextViewport(body).height};
  screen.target().stroke({static_cast<int16_t>(rail.x + 4), rail.y, 2, rail.height},
                         fui::Paint::solid(fui::Color::Black), 1);
  const uint8_t count = model.pageCount > 0 ? model.pageCount : 1;
  const int16_t thumbH = static_cast<int16_t>(
      rail.height / count > 28 ? rail.height / count : 28);
  const int16_t travel = static_cast<int16_t>(rail.height > thumbH ? rail.height - thumbH : 0);
  const int16_t offset = count > 1 ? static_cast<int16_t>(
      (static_cast<int32_t>(travel) * model.pageIndex) / (count - 1)) : 0;
  screen.target().fill({rail.x, static_cast<int16_t>(rail.y + offset), 10, thumbH},
                       fui::Paint::solid(fui::Color::Black));
  if (count > 1) {
    for (int i = 0; i < 6; ++i) {
      const int16_t top = static_cast<int16_t>(rail.y + (rail.height * i) / 6);
      const int16_t bottom = static_cast<int16_t>(rail.y + (rail.height * (i + 1)) / 6);
      const uint8_t page = static_cast<uint8_t>(
          (static_cast<uint32_t>(count - 1) * (2 * i + 1)) / 12);
      screen.frame().hit({static_cast<int16_t>(rail.x - 5), top, 20,
                          static_cast<int16_t>(bottom - top)}, ActionAwsScrollTo, page);
    }
  }
  char position[24];
  std::snprintf(position, sizeof(position), "%u / %u",
                static_cast<unsigned>(model.pageIndex + 1),
                static_cast<unsigned>(count));
  label(screen, {static_cast<int16_t>(body.x + body.width - 112),
                 static_cast<int16_t>(footer(screen) - 49), 90, 40}, position);
  // The action above the footer must never overlap text. The permanent
  // gesture hint also tells first-time touchscreen users how to navigate.
  label(screen, {static_cast<int16_t>(body.x + kMargin),
                 static_cast<int16_t>(footer(screen) - 49), 224, 40},
        model.german ? "WISCHEN | AWS HALTEN" : "SWIPE | HOLD AWS");
}

}  // namespace

fui::Rect awsStudyTextViewport(toybox::Screen screen) {
  // The same reservations chrome() makes, on a copy: the caller's screen and
  // the frame are untouched, and nothing is drawn.
  toybox::absoluteChrome(screen);
  screen.takeTop(screen.theme().headerHeight);
  return awsTextViewport(screen.body());
}

fui::Rect awsTextViewport(const fui::Rect& body) {
  const int16_t top = static_cast<int16_t>(body.y + 20);
  const int16_t footerTop = static_cast<int16_t>(body.bottom() - 96);
  return {static_cast<int16_t>(body.x + kMargin), static_cast<int16_t>(top + 54),
          static_cast<int16_t>(body.width - 2 * kMargin - 24),
          static_cast<int16_t>(footerTop - top - 136)};
}

uint8_t awsTextLines(const fui::DrawTarget& target, const fui::FontId font, const int16_t height) {
  const int16_t lineHeight = target.lineHeight(font);
  const int lines = lineHeight > 0 ? height / lineHeight : 1;
  return static_cast<uint8_t>(lines < 1 ? 1 : (lines > 16 ? 16 : lines));
}

AwsTextFit awsFitText(const fui::DrawTarget& target, const fui::FontId font, const char* text,
                      const fui::Rect& viewport, const uint32_t maxBytes) {
  AwsTextFit fit;
  if (!text || !*text || viewport.empty() || maxBytes == 0) return fit;
  // Same constants and the same pass-1 decisions as fui::layoutText, so the
  // prefix returned here is exactly what text() draws without an ellipsis.
  constexpr uint16_t kMaxLineBytes = 220;
  const fui::TextStyle s = style(font, fui::TextAlign::Left);
  const uint8_t maxLines = awsTextLines(target, font, viewport.height);
  char buf[kMaxLineBytes + 1];
  const auto widthOf = [&](const char* start, const uint16_t len) {
    std::memcpy(buf, start, len);
    buf[len] = '\0';
    return target.measureText(font, buf, s).width;
  };
  const char* p = text;
  uint8_t lines = 0;
  while (*p != '\0' && lines < maxLines) {
    const char* const before = p;
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    if (*p == '\n') {
      ++p;
    } else {
      const char* const lineStart = p;
      uint16_t fitLen = 0;
      const char* scan = p;
      while (true) {
        const char* wordEnd = scan;
        while (*wordEnd != '\0' && *wordEnd != ' ' && *wordEnd != '\n') ++wordEnd;
        const auto candidate = static_cast<uint16_t>(
            wordEnd - lineStart > kMaxLineBytes + 1 ? kMaxLineBytes + 1 : wordEnd - lineStart);
        if (candidate > kMaxLineBytes || widthOf(lineStart, candidate) > viewport.width) break;
        fitLen = candidate;
        if (*wordEnd == '\0' || *wordEnd == '\n') break;
        scan = wordEnd + 1;
        while (*scan == ' ') ++scan;
        if (*scan == '\0' || *scan == '\n') break;
      }
      if (fitLen == 0) {
        uint16_t len = 0;
        while (lineStart[len] != '\0' && lineStart[len] != ' ' && lineStart[len] != '\n') {
          auto next = static_cast<uint16_t>(len + 1);
          while ((static_cast<uint8_t>(lineStart[next]) & 0xC0u) == 0x80u) ++next;
          if (next > kMaxLineBytes || (len > 0 && widthOf(lineStart, next) > viewport.width)) break;
          len = next;
        }
        fitLen = len > 0 ? len : 1;
      }
      p = lineStart + fitLen;
      if (*p == '\n') ++p;
    }
    if (static_cast<uint32_t>(p - text) > maxBytes) {
      p = before;  // keep the page within the caller's buffer, whole lines only
      break;
    }
    ++lines;
  }
  if (p == text) {
    // A viewport or buffer too small for one line must still make progress.
    uint32_t n = 1;
    while (n < maxBytes && (static_cast<uint8_t>(text[n]) & 0xC0u) == 0x80u) ++n;
    p = text + n;
  }
  while (*p == ' ' && static_cast<uint32_t>(p - text) < maxBytes) ++p;
  fit.length = static_cast<uint32_t>(p - text);
  fit.more = *p != '\0';
  return fit;
}

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
  button(screen, {x, static_cast<int16_t>(body.y + 523), w, 61},
         model.german ? "LERNSTAND LOESCHEN" : "RESET PROGRESS",
         ActionAwsResetPrompt);
  footerPair(screen, model.german ? "PAKETE" : "PACKS", ActionStudyDecks,
             "START", ActionAwsBegin);
}

void buildAwsResetConfirm(toybox::Screen& screen, const AwsResetConfirmModel& model) {
  const bool german = model.german;
  chrome(screen, german ? "LERNSTAND ZURUECKSETZEN" : "RESET STUDY PROGRESS");
  const auto body = screen.body();
  const int16_t x = static_cast<int16_t>(body.x + kMargin);
  const int16_t w = static_cast<int16_t>(body.width - 2 * kMargin);
  label(screen, {x, static_cast<int16_t>(body.y + 80), w, 62},
        model.deckName ? model.deckName : "STUDY PACK");
  prose(screen, {x, static_cast<int16_t>(body.y + 190), w, 190},
        german ? "Alle gesehenen Fragen und Fehler-Markierungen in diesem Paket werden geloescht. Fragen und Bereichsfilter bleiben erhalten."
               : "All seen and missed-question marks for THIS pack will be cleared. Questions and topic filters are kept.");
  label(screen, {x, static_cast<int16_t>(body.y + 420), w, 52},
        german ? "NICHT RUECKGAENGIG" : "CANNOT BE UNDONE");
  footerPair(screen, german ? "ABBRECHEN" : "CANCEL", ActionAwsResetCancel,
             german ? "ZURUECKSETZEN" : "RESET", ActionAwsResetConfirm);
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


void buildAwsInlineGlossary(toybox::Screen& screen, const AwsPageModel& model) {
  if (!model.glossaryOverlay) return;
  const auto body = screen.body();
  // Overlay is drawn AFTER the question and footer. The backdrop consumes
  // touches outside it; a dismiss never selects or grades an underlying answer.
  screen.frame().hit({0, 0, 480, 800}, ActionAwsGlossaryResume);
  const fui::Rect panel{static_cast<int16_t>(body.x + 20),
                        static_cast<int16_t>(body.y + 166),
                        static_cast<int16_t>(body.width - 40), 338};
  screen.target().fill(panel, fui::Paint::solid(fui::Color::White));
  screen.target().stroke(panel, fui::Paint::solid(fui::Color::Black), 3);
  const fui::Rect header{static_cast<int16_t>(panel.x + 4),
                         static_cast<int16_t>(panel.y + 4),
                         static_cast<int16_t>(panel.width - 8), 56};
  screen.target().fill(header, fui::Paint::solid(fui::Color::Black));
  label(screen, header, model.glossaryTitle ? model.glossaryTitle : "AWS", true);
  prose(screen, {static_cast<int16_t>(panel.x + 18),
                 static_cast<int16_t>(panel.y + 82),
                 static_cast<int16_t>(panel.width - 36), 173},
        model.glossaryBody ? model.glossaryBody : "");
  button(screen, {static_cast<int16_t>(panel.x + 70),
                  static_cast<int16_t>(panel.bottom() - 64),
                  static_cast<int16_t>(panel.width - 140), 46},
         model.german ? "SCHLIESSEN" : "CLOSE", ActionAwsGlossaryResume);
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
  } else if (model.count == 0) {
    // A hold always answers, so the reader knows the gesture registered.
    label(screen, {x, static_cast<int16_t>(body.y + 14), w, 40},
          model.german ? "KEIN AWS-BEGRIFF" : "NO AWS TERM");
    prose(screen, {x, static_cast<int16_t>(body.y + 90), w, 200},
          model.german ? "Auf dieser Textseite wurde kein Begriff aus dem Offline-Glossar erkannt."
                       : "No term from the offline glossary was found on this text page.");
  } else {
    label(screen, {x, static_cast<int16_t>(body.y + 14), w, 40},
          model.german ? "ERKANNTE BEGRIFFE" : "RECOGNIZED TERMS");
    for (int i = 0; i < model.count && i < 6; ++i) {
      button(screen, {x, static_cast<int16_t>(body.y + 70 + i * 79), w, 65},
             model.terms[i], ActionAwsGlossaryTerm, static_cast<int16_t>(i));
    }
  }
  footerPair(screen, model.german ? "ZURUECK" : "BACK", ActionAwsGlossaryBack,
             model.german ? "WEITER LERNEN" : "RESUME", ActionAwsGlossaryResume);
}

}  // namespace triviaui
