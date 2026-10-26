#pragma once

#include "../ui/ToyboxScreen.h"
#include "AwsStudyPack.h"

namespace triviaui {
namespace fui = freeink::ui;

struct AwsMenuModel {
  uint32_t count = 0;
  uint32_t seen = 0;
  uint8_t enabled = trivia::kAwsAllDomains;
};

struct AwsDomainsModel {
  uint8_t enabled = trivia::kAwsAllDomains;
};

struct AwsPageModel {
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

void buildAwsMenu(toybox::Screen& screen, const AwsMenuModel& model);
void buildAwsDomains(toybox::Screen& screen, const AwsDomainsModel& model);
void buildAwsQuestion(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsOption(toybox::Screen& screen, const AwsPageModel& model);
void buildAwsExplanation(toybox::Screen& screen, const AwsPageModel& model);

}  // namespace triviaui
