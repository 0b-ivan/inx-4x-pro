#pragma once

#include "../ui/ToyboxScreen.h"
#include "AwsStudyPack.h"
#include "StudyDeckCatalog.h"

namespace triviaui {
namespace fui = freeink::ui;

struct StudyDeckListModel {
  bool german = false;
  bool remote = false;
  uint8_t total = 0;
  uint8_t offset = 0;
  uint8_t visible = 0;
  const char* names[trivia::kStudyDecksPerPage] = {};
  uint8_t indices[trivia::kStudyDecksPerPage] = {};
  int current = -1;
};

void buildStudyDeckList(toybox::Screen& screen, const StudyDeckListModel& model);

struct AwsMenuModel {
  bool german = false;
  const char* deckName = "AWS DEVOPS";
  uint32_t count = 0;
  uint32_t seen = 0;
  uint8_t enabled = trivia::kAwsAllDomains;
};

struct AwsDomainsModel {
  bool german = false;
  const char* topics[trivia::kAwsDomains] = {};
  uint8_t enabled = trivia::kAwsAllDomains;
};

struct AwsPageModel {
  bool german = false;
  const char* text = "";
  const char* headline = "";
  const char* subtitle = "";
  uint8_t domain = 0;
  uint8_t option = 0;
  uint8_t optionCount = 0;
  uint8_t correct = 0;
  uint8_t selected = 0;
  int right = 0;
  int asked = 0;
  bool more = false;
  bool hasQuestion = false;
  bool graded = false;
  bool correctAnswer = false;
  uint8_t pageIndex = 0;
  uint8_t pageCount = 1;
};

struct AwsSummaryModel {
  bool german = false;
  int asked = 0;
  int right = 0;
  uint32_t missed = 0;
  uint32_t seen = 0;
  uint32_t total = 0;
  bool review = false;
  uint8_t scroll = 0;  // one swipe up reveals more results, footer remains fixed
};
struct AwsGlossaryModel {
  bool german = false;
  uint8_t count = 0;
  const char* terms[6] = {};
  const char* description = nullptr;
  const char* term = nullptr;
};
void buildAwsGlossary(toybox::Screen& screen, const AwsGlossaryModel& model);
void buildAwsResetConfirm(toybox::Screen& screen, const char* deckName, bool german);

// The study text viewport inside screen.body(). The painter and the pager use
// the same rect, so a page is cut exactly where the wrapped text would end.
fui::Rect awsTextViewport(const fui::Rect& body);
// The viewport a study page builder will use, from a screen that has not had
// its chrome taken yet (render() fits the page before building the screen).
fui::Rect awsStudyTextViewport(toybox::Screen screen);
// Lines of `font` that fit in `height`, clamped to FreeInkUI's 16-line cap.
uint8_t awsTextLines(const fui::DrawTarget& target, fui::FontId font, int16_t height);

struct AwsTextFit {
  uint32_t length = 0;  // bytes of text that wrap into the viewport
  bool more = false;    // text remains after this page
};
// Mirrors fui::layoutText's greedy word wrap: the returned prefix renders in
// the viewport without an ellipsis. Never splits a UTF-8 codepoint, always
// makes progress, and never returns more than maxBytes.
AwsTextFit awsFitText(const fui::DrawTarget& target, fui::FontId font, const char* text,
                      const fui::Rect& viewport, uint32_t maxBytes);

void buildAwsSummary(toybox::Screen& screen, const AwsSummaryModel& model);
void buildAwsMenu(toybox::Screen& screen, const AwsMenuModel& model);
void buildAwsDomains(toybox::Screen& screen, const AwsDomainsModel& model);
void buildAwsQuestion(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsOption(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsExplanation(toybox::Screen& screen, const AwsPageModel& model);

}  // namespace triviaui
