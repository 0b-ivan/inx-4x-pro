// Host-only X4 Pro AWS screen interaction regression tests. All questions are
// synthetic. Test the exact painted hit regions at the 480x800 device size.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <FreeInkUI.h>
#include <FreeInkUICore.h>
#include "../../src/apps_local/trivia/AwsGlossary.h"
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
    assert(out.tap(90, 750).action == triviaui::ActionStudyDecks);
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
    assert(out.draw.contains("LERNFRAGE"));
    assert(out.draw.contains(model.text));
    assert(out.draw.contains(more ? "MEHR TEXT" : "ANTWORTEN"));
    assert(out.tap(80, 750).action == triviaui::ActionAwsFinish);
    assert(out.tap(350, 750).action == triviaui::ActionAwsPageNext);
    model.selected = 1;
    Rendered selected;
    render(selected, model, &triviaui::buildAwsQuestion);
    assert(selected.tap(370, 660).action == triviaui::ActionAwsSubmit);
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
    // Submit stays available even on an early answer card.
    assert(out.tap(370, 660).action == triviaui::ActionAwsSubmit);
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
      if (more) assert(out.tap(370, 660).action == triviaui::ActionAwsNextQuestion);
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
    assert(out.tap(220, 750).action == triviaui::ActionAwsReview);
    assert(out.tap(370, 750).action == triviaui::ActionAwsNextQuestion);
    m.scroll = 1;
    Rendered scrolled;
    render(scrolled, m, &triviaui::buildAwsSummary);
    assert(scrolled.draw.contains(german ? "10 ANTWORTEN IN DIESER RUNDE"
                                         : "10 ANSWERS IN THIS SESSION"));
    m.missed = 0; m.review = true;
    Rendered done;
    render(done, m, &triviaui::buildAwsSummary);
    assert(done.draw.contains(german ? "FEHLERTRAINING FERTIG" : "REVIEW COMPLETED"));
    assert(done.tap(220, 750).action == triviaui::ActionAwsBegin);
    assert(done.tap(370, 750).action == triviaui::ActionAwsNextQuestion);
  }
}

void testAwsGlossary() {
  const char* text = "Amazon S3 and AWS Lambda integrate with Amazon DynamoDB.";
  uint8_t matches[trivia::kAwsGlossaryMaxMatches] = {};
  const uint8_t count = trivia::awsGlossaryMatches(text, matches);
  assert(count == 3);
  triviaui::AwsGlossaryModel model;
  model.count = count;
  for (uint8_t i = 0; i < count; ++i)
    model.terms[i] = trivia::kAwsGlossary[matches[i]].term;
  Rendered choices;
  render(choices, model, &triviaui::buildAwsGlossary);
  assert(choices.draw.contains("AWS GLOSSARY"));
  assert(choices.tap(230, 190).action == triviaui::ActionAwsGlossaryTerm);
  assert(choices.tap(80, 750).action == triviaui::ActionAwsGlossaryBack);
  model.term = model.terms[0];
  model.description = trivia::kAwsGlossary[matches[0]].en;
  Rendered detail;
  render(detail, model, &triviaui::buildAwsGlossary);
  assert(detail.draw.contains(model.term));
  assert(detail.draw.contains(model.description));
  // RESUME returns straight to the study page from the detail as well;
  // BACK steps to the term list.
  assert(detail.tap(360, 750).action == triviaui::ActionAwsGlossaryResume);
  assert(detail.tap(80, 750).action == triviaui::ActionAwsGlossaryBack);
  assert(choices.tap(360, 750).action == triviaui::ActionAwsGlossaryResume);
  // A hold on a page without a known term still answers, with RESUME.
  for (bool german : {false, true}) {
    triviaui::AwsGlossaryModel none;
    none.german = german;
    Rendered nothing;
    render(nothing, none, &triviaui::buildAwsGlossary);
    assert(nothing.draw.contains(german ? "KEIN AWS-BEGRIFF" : "NO AWS TERM"));
    assert(nothing.tap(230, 190).action == 0);
    assert(nothing.tap(360, 750).action == triviaui::ActionAwsGlossaryResume);
  }
  // Short service names are recognised in running (also German) text.
  {
    uint8_t m[trivia::kAwsGlossaryMaxMatches] = {};
    const uint8_t n = trivia::awsGlossaryMatches(
        "Die IAM-Rolle darf in den S3-Bucket schreiben; Lambda liest die SQS-Queue.", m);
    assert(n == 4);
    bool iam = false, s3 = false, lambda = false, sqs = false;
    for (uint8_t i = 0; i < n; ++i) {
      const std::string t = trivia::kAwsGlossary[m[i]].term;
      iam |= t == "AWS IAM"; s3 |= t == "Amazon S3";
      lambda |= t == "AWS Lambda"; sqs |= t == "Amazon SQS";
    }
    assert(iam && s3 && lambda && sqs);
    assert(!trivia::awsContainsTerm("DIAMOND and MS3X", "IAM"));
    assert(!trivia::awsContainsTerm("MS3X", "S3"));
    // Every entry's match token is a whole word of its own displayed term,
    // so a page naming the full service always lists it.
    for (const auto& e : trivia::kAwsGlossary) assert(trivia::awsContainsTerm(e.term, e.match));
    // At most six matches even when the page names more services.
    uint8_t six[trivia::kAwsGlossaryMaxMatches] = {};
    assert(trivia::awsGlossaryMatches("S3 EC2 Lambda EKS ECS Fargate RDS DynamoDB", six) == 6);
  }
  assert(trivia::awsContainsTerm("aws lambda, and AWS LAMBDA.", "AWS Lambda"));
  assert(!trivia::awsContainsTerm("AWS LambdaFunction", "AWS Lambda"));
  assert(!trivia::awsContainsTerm("unused term", "AWS Lambda"));
  uint8_t empty[trivia::kAwsGlossaryMaxMatches] = {};
  assert(trivia::awsGlossaryMatches("No cloud services mentioned.", empty) == 0);
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
  assert(out.tap(220, 750).action == triviaui::ActionStudyNextPage);
  assert(out.tap(365, 750).action == triviaui::ActionStudyOnline);
  triviaui::StudyDeckListModel remote = model;
  remote.remote = true;
  Rendered online;
  render(online, remote, &triviaui::buildStudyDeckList);
  assert(online.draw.contains("GITHUB PAKETE"));
  assert(online.tap(240, 110).action == triviaui::ActionStudyRemoteSelect);
  assert(online.tap(90, 750).action == triviaui::ActionStudyDecks);
  assert(online.tap(220, 750).action == triviaui::ActionStudyRemoteNextPage);
  assert(online.tap(365, 750).action == triviaui::ActionStudyOnline);
}

// Concatenating every page must give back the text, with nothing lost or
// repeated, and each page must render with no ellipsis.
std::string pageThrough(const char* text, const fui::Rect& viewport, const uint32_t cap,
                        int& pages) {
  RecordingTarget metric;
  std::string joined;
  pages = 0;
  const char* p = text;
  while (*p) {
    const triviaui::AwsTextFit fit =
        triviaui::awsFitText(metric, toybox::kBodyFont, p, viewport, cap);
    assert(fit.length > 0 && fit.length <= cap);
    assert(fit.more == (p[fit.length] != '\0'));
    // No torn UTF-8 at either end of a page.
    assert((static_cast<uint8_t>(p[fit.length]) & 0xC0u) != 0x80u);
    const std::string page(p, fit.length);
    fui::TextStyle s;
    s.font = toybox::kBodyFont;
    s.maxLines = triviaui::awsTextLines(metric, toybox::kBodyFont, viewport.height);
    bool ellipsis = false;
    int lines = 0;
    fui::layoutText(metric, viewport, page.c_str(), s, [&](const char* line, fui::Rect) {
      ellipsis |= std::strstr(line, fui::TEXT_ELLIPSIS) != nullptr;
      ++lines;
    });
    assert(!ellipsis);
    assert(lines <= s.maxLines);
    joined += page;
    p += fit.length;
    ++pages;
    assert(pages < 500);
  }
  return joined;
}

std::string squash(const std::string& in) {
  std::string out;
  for (const char c : in) if (c != ' ' && c != '\n') out += c;
  return out;
}

void testAwsTextPaging() {
  // The pager runs before the builder takes its chrome; both must agree on
  // the rect, or a page cut for one height is drawn into another and ends
  // in an ellipsis (seen in the simulator with a 16- vs 15-line mismatch).
  {
    RecordingTarget draw;
    toybox::Interactions interactions;
    fui::DeviceContext context{};
    context.width = 480; context.height = 800; context.hasTouch = true;
    context.hasButtons = true;
    fui::InputSnapshot empty{};
    toybox::Frame frame(draw, context, empty, interactions);
    toybox::Screen screen(frame);
    const fui::Rect before = triviaui::awsStudyTextViewport(screen);
    triviaui::AwsPageModel model{};
    model.text = "Synthetic question text.";
    triviaui::buildAwsQuestion(screen, model);
    const fui::Rect after = triviaui::awsTextViewport(screen.body());
    assert(before.x == after.x && before.y == after.y &&
           before.width == after.width && before.height == after.height);
  }
  const fui::Rect body{0, 76, 480, 710};
  const fui::Rect viewport = triviaui::awsTextViewport(body);
  // The text area sits between the status rule and the gesture hint, and
  // never reaches the footer row or the GRADE NOW / NEXT QUESTION button.
  assert(viewport.y >= body.y + 74 && viewport.bottom() <= body.bottom() - 96 - 50);
  RecordingTarget metric;
  const uint8_t lines = triviaui::awsTextLines(metric, toybox::kBodyFont, viewport.height);
  assert(lines >= 10 && lines <= 16);

  std::string longScenario;
  for (int i = 0; i < 40; ++i)
    longScenario += "Ein Unternehmen nutzt Amazon S3, IAM-Rollen und AWS CodeDeploy fuer "
                    "Bereitstellungen ueber mehrere Regionen (café, naïve, Größe). ";
  longScenario += "arn:aws:iam::123456789012:role/" + std::string(400, 'x') + " Ende.\n\nNeuer Absatz.";
  int pages = 0;
  const std::string joined = pageThrough(longScenario.c_str(), viewport, 1399, pages);
  assert(squash(joined) == squash(longScenario));
  assert(joined == longScenario);  // byte-exact: offsets stack without gaps
  // A page is a screenful, not a fixed 260 characters: 12 x 56 cols ~ 670.
  assert(pages <= static_cast<int>(longScenario.size() / 400) + 2);

  // A buffer smaller than the viewport still pages whole lines, without loss.
  const std::string tight = pageThrough(longScenario.c_str(), viewport, 120, pages);
  assert(tight == longScenario);

  // Short text: one page, nothing more.
  const triviaui::AwsTextFit one =
      triviaui::awsFitText(metric, toybox::kBodyFont, "Kurz.", viewport, 1399);
  assert(one.length == 5 && !one.more);
  // Degenerate inputs never stall the pager.
  assert(triviaui::awsFitText(metric, toybox::kBodyFont, "", viewport, 1399).length == 0);
  const fui::Rect tiny{0, 0, 4, 2};
  const triviaui::AwsTextFit forced =
      triviaui::awsFitText(metric, toybox::kBodyFont, "\xC3\xA4" "bc", tiny, 1399);
  assert(forced.length == 2 && forced.more);
}

int main() {
  testAwsTextPaging();
  testStudyDeckList();
  testAwsSummary();
  testAwsGlossary();
  testAwsMenu();
  testAwsDomains();
  testAwsQuestion();
  testAwsOptionsAndGrading();
  testAwsExplanation();
  std::puts("PASS AWS X4 Pro 480x800 touch UI: menu, domains, pages, options and result");
}
