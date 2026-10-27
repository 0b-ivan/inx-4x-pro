#pragma once

// TRIVIA: 50,000 questions on the card, two ways to play them.
//
// The activity is the thin layer: it owns the pack files, the round and the
// input. The format and the round logic are in TriviaCore.h (freestanding) and
// the drawing is in TriviaScreens.cpp (freestanding). See docs/apps/trivia.md.

#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TriviaCore.h"
#include "AwsStudyPack.h"
#include "AwsStudyScreens.h"
#include "AwsGlossary.h"
#include "StudyDeckCatalog.h"
#include "TriviaPackLocale.h"
#include "TriviaScreens.h"

class TriviaActivity final : public Activity {
 public:
  TriviaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Trivia", renderer, mappedInput) {}
  ~TriviaActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t {
    Menu, Quizmaster, Solo, AwsMenu, AwsDomains, AwsQuestion, AwsOption,
    AwsExplanation, AwsSummary, AwsGlossary, AwsGlossaryDetail,
    StudyDecks, RemoteStudyDecks, Notice
  };
  // Which mode the HIDDEN notice came from. deal() decides whether it needs a
  // multiple-choice question by reading view_, so the notice has to put the
  // mode back before dealing or Quizmaster would start demanding distractors.
  View flagReturn_ = View::Menu;

  bool openPack();
  bool openAwsPack();
  void scanStudyDecks();
  void loadStudyTopics();
  bool studyPath(const char* ext, char* out, uint32_t capacity) const;
  void awsDeal();
  void awsSubmit();
  void awsNextPage();
  void awsPreviousPage();
  void openAwsGlossary();
  uint32_t awsMissedCount() const;
  const char* awsPageSource() const;
  void awsBuildPage();
  void saveAwsDomains() const;
  void onWifiChosen(bool connected);
  void runPackDownload();
  void onStudyWifiChosen(bool connected);
  void runStudyCatalogFetch();
  void runStudyPackFetch();
  void startStudyFetch();
  void showNotice(const char* headline, const char* body, const char* actionLabel = nullptr,
                  freeink::ui::ActionId action = 0);
  bool ensureState(uint32_t count, const char* statePath);
  void go(View next);
  void deal();
  void routeAction(int action, int value);

  View view_ = View::Menu;
  int selected_ = -1;
  int difficulty_ = 0;  // 0 = any

  // An independent licensed/private study deck. Never mixes its state with
  // the public trivia pack or the separate German translation state.
  trivia::StudyDeckCatalog deckCatalog_;
  trivia::StudyDeckCatalog remoteCatalog_;
  enum class StudyFetch : uint8_t { None, Catalog, Pack };
  StudyFetch studyFetch_ = StudyFetch::None;
  uint8_t remoteSelected_ = 0;
  uint8_t remotePage_ = 0;
  bool studyDownloadQueued_ = false;
  uint8_t selectedDeck_ = 0;
  uint8_t deckPage_ = 0;
  char studyTopics_[trivia::kAwsDomains][40] = {};
  trivia::AwsPack awsPack_;
  trivia::PackState awsState_;
  trivia::AwsQuestion awsQuestion_;
  trivia::AwsChooser awsChooser_;
  trivia::Score awsScore_;
  uint32_t awsCurrent_ = 0;
  uint32_t awsPageStart_ = 0;
  uint8_t awsSummaryScroll_ = 0;
  View awsGlossaryReturn_ = View::AwsQuestion;
  uint8_t awsGlossaryMatches_[trivia::kAwsGlossaryMaxMatches] = {};
  uint8_t awsGlossaryCount_ = 0;
  uint8_t awsGlossarySelected_ = 0;
  uint8_t awsOption_ = 0;
  uint8_t awsSelected_ = 0;
  uint8_t awsDomains_ = trivia::kAwsAllDomains;
  bool awsInstalled_ = false;
  bool awsHasQuestion_ = false;
  bool awsReviewMissed_ = false;
  bool awsHaveLast_ = false;
  bool awsAnswerCorrect_ = false;
  bool awsMore_ = false;
  char awsPageBuffer_[1200] = {};

  trivia::Pack pack_;
  trivia::PackState state_;
  trivia::Question question_;
  trivia::Choices choices_;
  trivia::Rng rng_{1};
  trivia::Chooser chooser_;
  trivia::Score score_;

  uint32_t current_ = 0;
  bool haveQuestion_ = false;
  bool germanContent_ = false;  // current pack, not the firmware UI locale
  bool revealed_ = false;
  int chosen_ = -1;

  char noticeHead_[24] = {};
  char noticeBody_[160] = {};
  const char* noticeAction_ = nullptr;
  freeink::ui::ActionId noticeActionId_ = 0;
  bool downloadCancel_ = false;
  bool downloadQueued_ = false;

  bool flashOnNextPaint_ = false;
  bool interactionsReady_ = false;
  toybox::Interactions interactions_;
};
