// Host-only X4 Pro AWS screen interaction regression tests. All questions are
// synthetic. Test the exact painted hit regions at the 480x800 device size.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <FreeInkUI.h>
#include "../../src/apps_local/trivia/AwsStudyScreens.h"
#include "../../src/apps_local/trivia/TriviaScreens.h"
#include "../../src/apps_local/ui/ToyboxScreen.h"

namespace fui = freeink::ui;

class RecordingTarget final : public fui::DrawTarget {
 public:
  std::vector<std::string> runs;
  fui::Size measureText(fui::FontId, const char* content, fui::TextStyle) const override {
    return {static_cast<int16_t>(content ? std::strlen(content) * 8 : 0), 29};
  }
  int16_t lineHeight(fui::FontId) const override { return 29; }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xff) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xff) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char* s, fui::TextStyle) override {
    if (s) runs.emplace_back(s);
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode,
              fui::Paint = {}, fui::Rotation = fui::Rotation::None) override {}
  bool contains(const char* s) const {
    for (const auto& text : runs) if (text == s) return true;
    return false;
  }
};

struct Rendered {
  RecordingTarget draw;
  toybox::Interactions interactions;
  fui::ActionEvent tap(const int x, const int y) {
    fui::InputSnapshot in{};
    in.touchReleased = true;
    in.touchX = static_cast<int16_t>(x);
    in.touchY = static_cast<int16_t>(y);
    return interactions.route(in);
  }
};

template<typename Model>
void render(Rendered& output, const Model& model,
            void (*builder)(toybox::Screen&, const Model&)) {
  fui::DeviceContext context{};
  context.width = 480; context.height = 800; context.hasTouch = true;
  context.hasButtons = true;
  fui::InputSnapshot empty{};
  toybox::Frame frame(output.draw, context, empty, output.interactions);
  toybox::Screen screen(frame);
  builder(screen, model);
  assert(!output.interactions.overflowed());
}

void testAwsMenu() {
  for (bool german : {false, true}) {
    triviaui::AwsMenuModel model{};
    model.german = german;
    model.count = 150;
    model.seen = 18;
    model.enabled = trivia::kAwsAllDomains;
    Rendered out;
    render(out, model, &triviaui::buildAwsMenu);
    assert(out.draw.contains(german ? "FRAGEN LERNEN" : "STUDY QUESTIONS"));
    assert(out.draw.contains(german ? "FEHLERTRAINING" : "REVIEW MISSED"));
    assert(out.draw.contains(german ? "BEREICHE WAEHLEN" : "SELECT DOMAINS"));
    assert(out.tap(230, 235).action == triviaui::ActionAwsBegin);
    assert(out.tap(230, 395).action == triviaui::ActionAwsReview);
    assert(out.tap(230, 515).action == triviaui::ActionAwsDomains);
    assert(out.tap(90, 750).action == triviaui::ActionAwsHome);
    assert(out.tap(360, 750).action == triviaui::ActionAwsBegin);
  }
}

void testAwsDomains() {
  for (bool german : {false, true}) {
    triviaui::AwsDomainsModel model{};
    model.german = german;
    model.enabled = 0b010101;
    Rendered out;
    render(out, model, &triviaui::buildAwsDomains);
    for (int i = 0; i < 6; ++i) {
      const auto action = out.tap(220, 76 + 12 + i * 89 + 35);
      assert(action.action == triviaui::ActionAwsToggleDomain);
      assert(action.value == i);
    }
    assert(out.tap(80, 750).action == triviaui::ActionAwsAllDomains);
    assert(out.tap(370, 750).action == triviaui::ActionAwsBackToMenu);
  }
}

void testAwsQuestion() {
  for (bool more : {false, true}) {
    triviaui::AwsPageModel model{};
    model.german = true;
    model.domain = 3;
    model.text = "A company must implement multi-region disaster recovery.";
    model.more = more;
    Rendered out;
    render(out, model, &triviaui::buildAwsQuestion);
    assert(out.draw.contains("AWS FRAGE"));
    assert(out.draw.contains(model.text));
    assert(out.draw.contains(more ? "MEHR TEXT" : "ANTWORTEN"));
    assert(out.tap(80, 750).action == triviaui::ActionAwsFinish);
    assert(out.tap(350, 750).action == triviaui::ActionAwsPageNext);
  }
}

void testAwsOptionsAndGrading() {
  triviaui::AwsPageModel m{};
  m.optionCount = 6;
  m.option = 5;
  m.text = "Amazon S3 replication";
  m.correct = 0b001101; // synthetic multi-answer key
  m.selected = 0;
  for (bool more : {false, true}) {
    m.more = more;
    Rendered out;
    render(out, m, &triviaui::buildAwsOption);
    assert(out.draw.contains("MARK"));
    assert(out.tap(65, 750).action == triviaui::ActionAwsToggleOption);
    assert(out.tap(220, 750).action == triviaui::ActionAwsPageBack);
    // At the final option with no answer, the right button must select rather
    // than silently submit an empty answer.
    assert(out.tap(395, 750).action ==
           (more ? triviaui::ActionAwsPageNext : triviaui::ActionAwsToggleOption));
  }
  m.more = false; m.selected = 0b000001;
  {
    Rendered out;
    render(out, m, &triviaui::buildAwsOption);
    assert(out.draw.contains("GRADE"));
    assert(out.tap(395, 750).action == triviaui::ActionAwsSubmit);
  }
  m.option = 1; m.selected = 0b000001;
  {
    Rendered out;
    render(out, m, &triviaui::buildAwsOption);
    assert(out.draw.contains("NEXT"));
    assert(out.tap(395, 750).action == triviaui::ActionAwsPageNext);
  }
  // Verify that selecting / removing the active option is visually explained.
  m.option = 0;
  {
    Rendered out;
    render(out, m, &triviaui::buildAwsOption);
    assert(out.draw.contains("UNMARK"));
  }
}

void testAwsExplanation() {
  triviaui::AwsPageModel m{};
  m.optionCount = 4;
  m.correct = 0b0101; m.selected = 0b0001;
  m.asked = 3; m.right = 2;
  m.correctAnswer = false;
  m.text = "Explanation: AWS CloudFormation manages infrastructure as code.";
  for (bool german : {false, true}) {
    for (bool more : {false, true}) {
      m.german = german; m.more = more;
      Rendered out;
      render(out, m, &triviaui::buildAwsExplanation);
      assert(out.draw.contains(m.text));
      assert(out.draw.contains(more ? (german ? "MEHR TEXT" : "MORE TEXT")
                                    : (german ? "NAECHSTE" : "NEXT QUESTION")));
      assert(out.tap(90, 750).action == triviaui::ActionAwsFinish);
      assert(out.tap(350, 750).action == triviaui::ActionAwsPageNext);
    }
  }
}

void testAwsSummary() {
  for (bool german : {false, true}) {
    triviaui::AwsSummaryModel m{};
    m.german = german;
    m.asked = 10; m.right = 7; m.seen = 42; m.total = 150; m.missed = 3;
    Rendered out;
    render(out, m, &triviaui::buildAwsSummary);
    assert(out.draw.contains(german ? "7 VON 10 RICHTIG" : "7 OF 10 CORRECT"));
    assert(out.draw.contains("70%"));
    assert(out.draw.contains(german ? "3 FEHLERFRAGEN OFFEN" : "3 MISSED QUESTIONS"));
    assert(out.tap(80, 750).action == triviaui::ActionAwsBackToMenu);
    assert(out.tap(350, 750).action == triviaui::ActionAwsReview);
    m.missed = 0; m.review = true;
    Rendered done;
    render(done, m, &triviaui::buildAwsSummary);
    assert(done.draw.contains(german ? "FEHLERTRAINING FERTIG" : "REVIEW COMPLETED"));
    assert(done.tap(350, 750).action == triviaui::ActionAwsBegin);
  }
}

void testStudyDeckList() {
  triviaui::StudyDeckListModel model{};
  model.german = true; model.total = 8; model.visible = 6;
  model.current = 2;
  constexpr const char* items[] = {
      "DOP-C02", "SAA-C03", "CLF-C02", "LINUX", "K8S", "NETWORKING"};
  for (int i = 0; i < 6; ++i) {
    model.names[i] = items[i];
    model.indices[i] = static_cast<uint8_t>(i);
  }
  Rendered out;
  render(out, model, &triviaui::buildStudyDeckList);
  assert(out.draw.contains("FRAGENPAKETE"));
  for (int i = 0; i < 6; ++i) {
    const auto event = out.tap(240, 110 + 89*i);
    assert(event.action == triviaui::ActionStudySelect);
    assert(event.value == i);
  }
  assert(out.tap(90, 750).action == triviaui::ActionAwsHome);
  assert(out.tap(365, 750).action == triviaui::ActionStudyNextPage);
}

int main() {
  testStudyDeckList();
  testAwsSummary();
  testAwsMenu();
  testAwsDomains();
  testAwsQuestion();
  testAwsOptionsAndGrading();
  testAwsExplanation();
  std::puts("PASS AWS X4 Pro 480x800 touch UI: menu, domains, pages, options and result");
}
