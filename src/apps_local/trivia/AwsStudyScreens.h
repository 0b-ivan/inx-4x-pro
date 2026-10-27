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

void buildAwsSummary(toybox::Screen& screen, const AwsSummaryModel& model);
void buildAwsMenu(toybox::Screen& screen, const AwsMenuModel& model);
void buildAwsDomains(toybox::Screen& screen, const AwsDomainsModel& model);
void buildAwsQuestion(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsOption(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsExplanation(toybox::Screen& screen, const AwsPageModel& model);

}  // namespace triviaui
