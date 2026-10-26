#include "TriviaActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kDir = "/trivia";
// Legacy English pack path stays unchanged; a German pack is installed
// separately as /trivia/pack-de.dat. Neither language replaces the other.
constexpr const char* kPackPath = trivia::kEnglishPackLocation.packPath;
constexpr const char* kPartPath = "/trivia/pack.dat.part";
// A rolling PRERELEASE, so the OTA's releases/latest can never see it and a
// 6MB question pack never lands in a firmware update. Same arrangement as the
// xkcd archive; see docs/apps/trivia-pack-format.md.
constexpr const char* kPackUrl = "https://github.com/ma-r-s/crossplay/releases/download/trivia-pack/pack.dat";

// One byte: the difficulty filter. It lived only in the activity, which is
// deleted on exit, so leaving Trivia and coming back silently put a player who
// had chosen Level 4 back on "Any" with nothing on screen to say so.
constexpr const char* kPrefsPath = "/trivia/prefs";
constexpr const char* kAwsPackPath = "/trivia/aws/pack.dat";
constexpr const char* kAwsStatePath = "/trivia/aws/pack.state";
constexpr const char* kAwsStateIdPath = "/trivia/aws/pack.state.id";
constexpr const char* kAwsPrefsPath = "/trivia/aws/prefs";
constexpr uint32_t kAwsPageCharacters = 260;

// A ByteSource over a HalFile. Every read seeks first: an index entry and the
// record it points at are in different places, so sequential reads are the
// exception rather than the rule.
class FileSource final : public trivia::WritableByteSource {
 public:
  void attach(HalFile& file) {
    file_ = &file;
    size_ = static_cast<uint32_t>(file.size());
  }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset > size_ || offset + length > size_) return false;
    if (!file_->seekSet(offset)) return false;
    return file_->read(dst, length) == static_cast<int>(length);
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset + length > size_) return false;  // state never grows
    if (!file_->seekSet(offset)) return false;
    return file_->write(static_cast<const uint8_t*>(src), length) == length;
  }
  bool flush() override {
    if (file_ != nullptr) file_->flush();
    return true;
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile* file_ = nullptr;
  uint32_t size_ = 0;
};

HalFile g_packFile;
HalFile g_stateFile;
FileSource g_packSource;
FileSource g_stateSource;
HalFile g_awsPackFile;
HalFile g_awsStateFile;
FileSource g_awsPackSource;
FileSource g_awsStateSource;

}  // namespace

std::unique_ptr<Activity> TriviaActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TriviaActivity>(renderer, mappedInput);
}

bool TriviaActivity::ensureState(const uint32_t count, const char* statePath) {
  g_stateFile = Storage.open(statePath, O_RDWR);
  if (!g_stateFile.isOpen() || static_cast<uint32_t>(g_stateFile.size()) != count) {
    // Missing, or sized for a different pack. Close the old handle before
    // truncating: keeping it open can prevent overwriting on the actual SD HAL.
    g_stateFile.close();
    // The pack was replaced under its state or is being installed for the first time.
    // Rewriting loses which questions have been seen, which is the right trade:
    // reading a stale byte for a question it does not describe is worse.
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", statePath, fresh)) {
      LOG_ERR("TRIVIA", "Cannot create %s", statePath);
      return false;
    }
    uint8_t zeros[256] = {};
    for (uint32_t written = 0; written < count;) {
      const uint32_t chunk = (count - written) < sizeof(zeros) ? (count - written) : sizeof(zeros);
      if (fresh.write(zeros, chunk) != chunk) {
        LOG_ERR("TRIVIA", "Short write creating %s", statePath);
        return false;
      }
      written += chunk;
    }
    fresh.flush();
    fresh.close();
    g_stateFile = Storage.open(statePath, O_RDWR);
  }
  if (!g_stateFile.isOpen()) return false;
  g_stateSource.attach(g_stateFile);
  return state_.open(g_stateSource, count);
}

bool TriviaActivity::openPack() {
  const bool wantsGerman = I18N.getLanguage() == Language::DE;
  const auto tryOpen = [this](const trivia::PackLocation& location, bool german) {
    g_packFile.close();
    g_stateFile.close();
    if (!Storage.openFileForRead("TRIVIA", location.packPath, g_packFile)) return false;
    g_packSource.attach(g_packFile);
    if (!pack_.open(g_packSource) || pack_.flags() != (german ? trivia::kPackGermanFlag : 0)) {
      LOG_ERR("TRIVIA", "Unreadable question pack: %s", location.packPath);
      g_packFile.close();
      return false;
    }
    if (!ensureState(pack_.count(), location.statePath)) {
      g_packFile.close();
      g_stateFile.close();
      return false;
    }
    germanContent_ = german;
    chooser_.begin(pack_, state_, rng_);
    return true;
  };

  // A German UI must never misidentify the legacy English data as translated.
  // Until an actual German pack is installed, continue to allow English play.
  if (trivia::shouldUseGerman(wantsGerman, Storage.exists(trivia::kGermanPackLocation.packPath)) &&
      tryOpen(trivia::kGermanPackLocation, true)) return true;
  if (tryOpen(trivia::kEnglishPackLocation, false)) return true;
  LOG_ERR("TRIVIA", "No readable German or English question pack");
  return false;
}

bool TriviaActivity::openAwsPack() {
  g_awsPackFile.close();
  g_awsStateFile.close();
  awsInstalled_ = false;
  if (!Storage.openFileForRead("TRIVIA", kAwsPackPath, g_awsPackFile)) return false;
  g_awsPackSource.attach(g_awsPackFile);
  if (!awsPack_.open(g_awsPackSource)) {
    g_awsPackFile.close();
    LOG_ERR("TRIVIA", "Invalid AWS study pack");
    return false;
  }
  bool sameContent = false;
  {
    HalFile stamp = Storage.open(kAwsStateIdPath, O_RDONLY);
    if (stamp.isOpen() && stamp.size() == 4) {
      uint8_t id[4] = {};
      const uint32_t fingerprint = awsPack_.fingerprint();
      if (stamp.read(id, 4) == 4) {
        sameContent = id[0] == static_cast<uint8_t>(fingerprint) &&
                      id[1] == static_cast<uint8_t>(fingerprint >> 8) &&
                      id[2] == static_cast<uint8_t>(fingerprint >> 16) &&
                      id[3] == static_cast<uint8_t>(fingerprint >> 24);
      }
    }
  }
  g_awsStateFile = Storage.open(kAwsStatePath, O_RDWR);
  if (!sameContent || !g_awsStateFile.isOpen() ||
      static_cast<uint32_t>(g_awsStateFile.size()) != awsPack_.count()) {
    g_awsStateFile.close();
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", kAwsStatePath, fresh)) return false;
    uint8_t zeros[256] = {};
    for (uint32_t written = 0; written < awsPack_.count();) {
      const uint32_t remaining = awsPack_.count() - written;
      const uint32_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
      if (fresh.write(zeros, chunk) != chunk) {
        fresh.close();
        return false;
      }
      written += chunk;
    }
    fresh.flush();
    fresh.close();
    HalFile stamp;
    if (!Storage.openFileForWrite("TRIVIA", kAwsStateIdPath, stamp)) return false;
    const uint32_t fingerprint = awsPack_.fingerprint();
    const uint8_t id[4] = {
        static_cast<uint8_t>(fingerprint),
        static_cast<uint8_t>(fingerprint >> 8),
        static_cast<uint8_t>(fingerprint >> 16),
        static_cast<uint8_t>(fingerprint >> 24)};
    if (stamp.write(id, sizeof(id)) != sizeof(id)) return false;
    stamp.flush();
    stamp.close();
    g_awsStateFile = Storage.open(kAwsStatePath, O_RDWR);
  }
  if (!g_awsStateFile.isOpen()) return false;
  g_awsStateSource.attach(g_awsStateFile);
  if (!awsState_.open(g_awsStateSource, awsPack_.count())) return false;
  HalFile prefs = Storage.open(kAwsPrefsPath, O_RDONLY);
  awsDomains_ = trivia::kAwsAllDomains;
  if (prefs.isOpen() && prefs.size() == 1) {
    uint8_t mask = 0;
    if (prefs.read(&mask, 1) == 1 && !(mask & ~trivia::kAwsAllDomains))
      awsDomains_ = mask;
  }
  awsChooser_.begin(awsPack_, awsState_, rng_);
  awsInstalled_ = true;
  return true;
}

void TriviaActivity::saveAwsDomains() const {
  HalFile prefs;
  if (!Storage.openFileForWrite("TRIVIA", kAwsPrefsPath, prefs)) return;
  prefs.write(&awsDomains_, 1);
}

namespace {

int loadDifficulty() {
  HalFile f = Storage.open(kPrefsPath, O_RDONLY);
  if (!f.isOpen() || f.size() < 1) return 0;
  uint8_t b = 0;
  if (f.read(&b, 1) != 1) return 0;
  // A file written by a future build, or a corrupt byte, must not select a
  // difficulty that filters every question out of the pack.
  return (b <= trivia::kDifficulties) ? static_cast<int>(b) : 0;
}

void saveDifficulty(const int difficulty) {
  HalFile f;
  if (!Storage.openFileForWrite("TRIVIA", kPrefsPath, f)) return;
  const uint8_t b = static_cast<uint8_t>(difficulty);
  f.write(&b, 1);
}

}  // namespace

void TriviaActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  rng_.seed(static_cast<uint32_t>(millis()) | 1u);
  difficulty_ = loadDifficulty();

  const bool haveTrivia = openPack();
  const bool haveAws = openAwsPack();
  if (haveTrivia || haveAws) {
    view_ = View::Menu;
    LOG_INF("TRIVIA", "Pack open: trivia=%u aws=%u",
            static_cast<unsigned>(pack_.count()),
            static_cast<unsigned>(awsPack_.count()));
  } else {
    showNotice("NO QUESTIONS",
               I18N.getLanguage() == Language::DE
                   ? "A German question pack is not installed. The available download contains English questions."
                   : "There is no question pack on this card yet. Connect to WiFi and fetch one; it is about 6MB.",
               I18N.getLanguage() == Language::DE ? "GET ENGLISH QUESTIONS" : "GET THE QUESTIONS",
               triviaui::ActionGetPack);
  }

  flashOnNextPaint_ = true;
  requestUpdate();
}

void TriviaActivity::onExit() {
  g_packFile.close();
  g_stateFile.close();
  g_awsPackFile.close();
  g_awsStateFile.close();
  Activity::onExit();
}

void TriviaActivity::showNotice(const char* headline, const char* body, const char* actionLabel,
                                const freeink::ui::ActionId action) {
  std::snprintf(noticeHead_, sizeof(noticeHead_), "%s", headline);
  // Guarded because the natural call is the dangerous one: reading the current
  // body, deciding to keep it, and passing it straight back makes this an
  // snprintf of a buffer onto itself, which is undefined behaviour for
  // overlapping copies. Aliasing means "leave it as it is", so skipping is
  // also the right answer semantically, not merely the safe one.
  if (body != noticeBody_) {
    std::snprintf(noticeBody_, sizeof(noticeBody_), "%s", body);
  }
  noticeAction_ = actionLabel;
  noticeActionId_ = action;
  view_ = View::Notice;
  interactionsReady_ = false;
  flashOnNextPaint_ = true;
  requestUpdate();
}

// One asset, to a .part name, renamed only once it is whole -- so a torn
// download can never masquerade as a corrupt pack. The app simply finds no
// pack, exactly as before the attempt.
//
// Synchronous: loop() is blocked but the render task is not, so progress paints
// through requestUpdateAndWait(). The input pump in the progress callback is
// the sanctioned exception to the one-pump rule -- nothing else pumps while
// this blocks, and without it Back could not cancel a multi-minute download.
void TriviaActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Cancelling the picker is a decision, not a failure: say what did not
    // happen and leave the button that starts it again.
    showNotice("NO WIFI", "The pack needs WiFi to download. The card is unchanged.", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }
  // Queued rather than run here: the download blocks for minutes, and this is
  // the result handler of the activity that is still unwinding.
  downloadQueued_ = true;
}

void TriviaActivity::runPackDownload() {
  // exists() first, because SdFat's mkdir returns FALSE for a directory that
  // is already there. Treating that as failure meant the second attempt at a
  // download could never succeed: the first one creates /trivia, and every run
  // after it reports NO ROOM on a card with gigabytes free.
  //
  // Found on hardware and invisible in the simulator, whose SD is an ordinary
  // host directory where mkdir on an existing path succeeds. Every other
  // caller in this fork already does it this way -- StudyActivity, ScreenshotUtil,
  // BookmarkFile -- and this was the one that invented its own.
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) {
    showNotice("NO ROOM", "Could not create /trivia on the card. Is the card in, and writable?", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }

  // Ask the card how much room is left BEFORE writing ~6MB to it, because this
  // app is not the only thing on the card. A card filled by a trivia pack is a
  // card where Study's review log cannot be written, and that failure loses
  // answers rather than refusing -- the cost of overfilling lands on a different
  // app, silently, later.
  //
  // The decision itself lives in TriviaCore so it can be tested: this file
  // includes WiFi.h and cannot be built on the host at all.
  uint64_t freeNow = 0;
  const bool queryOk = Storage.freeBytes(freeNow);
  switch (trivia::roomFor(queryOk, freeNow, trivia::kPackFreeFloorBytes)) {
    case trivia::Room::Unknown:
      // NOT the same screen as NO ROOM: freeBytes() returns false for "could not
      // answer" and never for "full". Saying the card is full when we do not know
      // that would be the same conflation the HAL call exists to prevent.
      // The body must describe what the BUTTON does. The first version said the
      // card "may need re-seating" beside a button marked TRY AGAIN: the
      // instruction and the only available action were about different things,
      // and it sent people to fiddle with hardware for a fault that is usually
      // transient. Not asserting anything about the physical slot, which this
      // code cannot know and nobody has checked.
      showNotice("CAN'T TELL",
                 "The card did not answer when asked how much room is left, so nothing was written. "
                 "Trying again usually works.",
                 "TRY AGAIN", triviaui::ActionGetPack);
      return;
    case trivia::Room::TooSmall: {
      // Local buffer: showNotice snprintf()s body INTO noticeBody_, so passing
      // noticeBody_ as the body argument would be an overlapping self-copy.
      char body[160];
      // Says what would make the retry succeed. Reporting both numbers and then
      // offering only TRY AGAIN told the user they were stuck while implying
      // the button might help; it is the only action on screen, so the sentence
      // has to name the thing that makes it work.
      std::snprintf(body, sizeof(body),
                    "The questions need about %u MB free and the card has %u MB. "
                    "Delete something from the card, then try again. Nothing was written.",
                    static_cast<unsigned>(trivia::kPackFreeFloorBytes >> 20),
                    static_cast<unsigned>(freeNow >> 20));
      showNotice("NO ROOM", body, "TRY AGAIN", triviaui::ActionGetPack);
      return;
    }
    case trivia::Room::Ok:
      break;
  }


  g_packFile.close();
  g_stateFile.close();
  downloadCancel_ = false;

  size_t lastPainted = 0;
  const auto progress = [this, &lastPainted](const size_t got, const size_t total) {
    // Every ~1MB: each paint is an e-ink refresh, and finer steps would spend
    // more time refreshing than downloading.
    if (got - lastPainted >= 1024u * 1024u || (total > 0 && got == total)) {
      lastPainted = got;
      if (total > 0) {
        std::snprintf(noticeBody_, sizeof(noticeBody_), "Fetching the questions: %u of %u MB. Back stops it.",
                      static_cast<unsigned>(got >> 20), static_cast<unsigned>(total >> 20));
      } else {
        std::snprintf(noticeBody_, sizeof(noticeBody_), "Fetching the questions: %u MB so far. Back stops it.",
                      static_cast<unsigned>(got >> 20));
      }
      requestUpdateAndWait();
    }
    mappedInput.update();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) downloadCancel_ = true;
    if (mappedInput.wasHomeGesture()) downloadCancel_ = true;
  };

  std::snprintf(noticeHead_, sizeof(noticeHead_), "%s", "FETCHING");
  std::snprintf(noticeBody_, sizeof(noticeBody_), "%s", "Starting. Back stops it.");
  noticeAction_ = nullptr;
  requestUpdateAndWait();

  const auto err = HttpDownloader::downloadToFile(kPackUrl, kPartPath, progress, &downloadCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(kPartPath);
    // Every one of these offers TRY AGAIN. A screen that reports a failure and
    // gives you nothing to press is a dead end -- the user's only way out is to
    // leave the app, and nothing on screen says so. Get Books shipped exactly
    // that and Mario found it on the device.
    if (err == HttpDownloader::ABORTED) {
      showNotice("STOPPED", "Download stopped. Nothing was kept.", "TRY AGAIN", triviaui::ActionGetPack);
    } else if (err == HttpDownloader::FILE_ERROR) {
      showNotice("CARD TROUBLE", "The card would not take the file. Nothing was kept.", "TRY AGAIN",
                 triviaui::ActionGetPack);
    } else {
      showNotice("NO ANSWER", "The download did not answer. The card is unchanged.", "TRY AGAIN",
                 triviaui::ActionGetPack);
    }
    return;
  }

  Storage.remove(kPackPath);  // a half pack from an earlier era must not block the rename
  if (!Storage.rename(kPartPath, kPackPath)) {
    showNotice("CARD TROUBLE", "Downloaded, but the card refused the final rename.", "TRY AGAIN",
               triviaui::ActionGetPack);
    return;
  }

  // The state file describes the OLD pack. openPack rewrites it when the
  // question count no longer matches, which loses which questions have been
  // seen -- the right trade against reading a stale byte for a question it
  // does not describe.
  if (!openPack()) {
    showNotice("BAD PACK", "Downloaded, but the pack did not open.", "TRY AGAIN", triviaui::ActionGetPack);
    return;
  }
  char body[96];
  std::snprintf(body, sizeof(body), "%u questions on the card. Ready when you are.",
                static_cast<unsigned>(pack_.count()));
  showNotice("READY", body, "PLAY", triviaui::ActionMenuRow);
  selected_ = -1;
}

void TriviaActivity::go(const View next) {
  view_ = next;
  interactionsReady_ = false;
  flashOnNextPaint_ = true;  // a mode change is a page turn, so spend the flash
  requestUpdate();
}

void TriviaActivity::deal() {
  const bool solo = view_ == View::Solo;
  revealed_ = false;
  chosen_ = -1;
  haveQuestion_ = false;

  uint32_t index = 0;
  if (!chooser_.next(index, solo, difficulty_)) {
    LOG_ERR("TRIVIA", "No question available (difficulty %d, solo %d)", difficulty_, solo ? 1 : 0);
    return;
  }
  if (!pack_.read(index, question_)) {
    LOG_ERR("TRIVIA", "Unreadable record %u", static_cast<unsigned>(index));
    return;
  }
  current_ = index;
  haveQuestion_ = true;
  state_.setFlag(index, trivia::kSeen);
  if (solo) trivia::buildChoices(question_, rng_, choices_);
  requestUpdate();
}

void TriviaActivity::awsDeal() {
  uint32_t index = 0;
  awsHasQuestion_ = false;
  if (!awsChooser_.next(index, awsDomains_, awsReviewMissed_) ||
      !awsPack_.read(index, awsQuestion_)) {
    showNotice("NO AWS QUESTIONS",
               awsReviewMissed_ ? "No previously missed questions in your enabled domains."
                                : "No matching questions. Enable more AWS domains.",
               "DOMAINS", triviaui::ActionAwsDomains);
    return;
  }
  awsCurrent_ = index;
  awsHasQuestion_ = true;
  awsPageStart_ = 0;
  awsOption_ = 0;
  awsSelected_ = 0;
  awsAnswerCorrect_ = false;
  awsState_.setFlag(index, trivia::kSeen);
  go(View::AwsQuestion);
}

const char* TriviaActivity::awsPageSource() const {
  switch (view_) {
    case View::AwsQuestion: return awsQuestion_.question();
    case View::AwsOption: return awsQuestion_.option(awsOption_);
    case View::AwsExplanation: return awsQuestion_.explanation();
    default: return "";
  }
}

void TriviaActivity::awsBuildPage() {
  const char* text = awsPageSource();
  const trivia::AwsTextPage page = trivia::awsTextPage(
      text, awsPageStart_, kAwsPageCharacters);
  uint32_t bytes = page.length;
  if (bytes >= sizeof(awsPageBuffer_)) bytes = sizeof(awsPageBuffer_) - 1;
  if (bytes != 0) std::memcpy(awsPageBuffer_, text + page.start, bytes);
  awsPageBuffer_[bytes] = '\0';
  awsMore_ = page.more;
}

void TriviaActivity::awsSubmit() {
  // Marking is deliberate even for single-choice; a missed tap cannot
  // silently count as a wrong answer or advance to the next question.
  if (!awsHasQuestion_ || awsSelected_ == 0) return;
  awsAnswerCorrect_ = awsQuestion_.evaluate(awsSelected_);
  awsScore_.record(awsAnswerCorrect_);
  if (!awsAnswerCorrect_) awsState_.setFlag(awsCurrent_, trivia::kAwsMissed);
  awsPageStart_ = 0;
  go(View::AwsExplanation);
}

void TriviaActivity::awsNextPage() {
  if (!awsHasQuestion_) return;
  const trivia::AwsTextPage page =
      trivia::awsTextPage(awsPageSource(), awsPageStart_, kAwsPageCharacters);
  if (page.more && page.length > 0) {
    awsPageStart_ += page.length;
    requestUpdate();
    return;
  }
  if (view_ == View::AwsQuestion) {
    awsPageStart_ = 0;
    go(View::AwsOption);
  } else if (view_ == View::AwsOption) {
    if (awsOption_ + 1 < awsQuestion_.optionCount()) {
      ++awsOption_;
      awsPageStart_ = 0;
      requestUpdate();
    } else {
      awsSubmit();
    }
  } else if (view_ == View::AwsExplanation) {
    awsDeal();
  }
}

void TriviaActivity::awsPreviousPage() {
  if (view_ != View::AwsOption) return;
  if (awsPageStart_ > 0) {
    uint32_t previous = 0, cursor = 0;
    while (cursor < awsPageStart_) {
      previous = cursor;
      const trivia::AwsTextPage page =
          trivia::awsTextPage(awsPageSource(), cursor, kAwsPageCharacters);
      if (page.length == 0) break;
      cursor += page.length;
    }
    awsPageStart_ = previous;
    requestUpdate();
  } else if (awsOption_ > 0) {
    --awsOption_;
    awsPageStart_ = 0;
    requestUpdate();
  } else {
    awsPageStart_ = 0;
    go(View::AwsQuestion);
  }
}

void TriviaActivity::routeAction(const int action, const int value) {
  switch (action) {
    case triviaui::ActionGetPack:
      // The radio first. Entering the TLS stack with WiFi never started is not
      // a failed download, it is a panic: the socket layer takes a mutex that
      // does not exist yet and FreeRTOS asserts on the null handle
      // (xQueueSemaphoreTake, queue.c:1709). The notice this button sits under
      // has always said "connect to WiFi and fetch one"; nothing did.
      WiFi.mode(WIFI_STA);
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
      break;
    case triviaui::ActionMenuRow:
      if (view_ == View::Notice) {  // the READY notice's PLAY button
        go(View::Menu);
        break;
      }
      selected_ = value;
      if (value == 0) {
        go(View::Quizmaster);
        deal();
      } else if (value == 1) {
        score_.reset();
        go(View::Solo);
        deal();
      } else if (value == 2) {
        difficulty_ = (difficulty_ + 1) % (trivia::kDifficulties + 1);
        saveDifficulty(difficulty_);
        requestUpdate();
      } else if (value == 3) {
        if (awsInstalled_) {
          go(View::AwsMenu);
        } else {
          showNotice("AWS PACK MISSING", "Install a private AWS study pack at /trivia/aws/pack.dat.",
                     "BACK", triviaui::ActionAwsHome);
        }
      }
      break;
    case triviaui::ActionAwsHome:
      go(View::Menu);
      break;
    case triviaui::ActionAwsBackToMenu:
      go(View::AwsMenu);
      break;
    case triviaui::ActionAwsDomains:
      go(View::AwsDomains);
      break;
    case triviaui::ActionAwsAllDomains:
      awsDomains_ = trivia::kAwsAllDomains;
      saveAwsDomains();
      requestUpdate();
      break;
    case triviaui::ActionAwsToggleDomain:
      if (value >= 0 && value < trivia::kAwsDomains) {
        awsDomains_ ^= static_cast<uint8_t>(1u << value);
        saveAwsDomains();
        requestUpdate();
      }
      break;
    case triviaui::ActionAwsBegin:
    case triviaui::ActionAwsReview:
      if (!awsInstalled_) break;
      if (awsDomains_ == 0) {
        showNotice("NO DOMAINS", "Enable at least one AWS exam domain.",
                   "DOMAINS", triviaui::ActionAwsDomains);
        break;
      }
      awsScore_.reset();
      awsReviewMissed_ = action == triviaui::ActionAwsReview;
      awsDeal();
      break;
    case triviaui::ActionAwsPageNext:
      awsNextPage();
      break;
    case triviaui::ActionAwsPageBack:
      awsPreviousPage();
      break;
    case triviaui::ActionAwsToggleOption:
      if (view_ == View::AwsOption && awsHasQuestion_) {
        const uint8_t bit = static_cast<uint8_t>(1u << awsOption_);
        awsSelected_ = awsQuestion_.multiple()
                          ? static_cast<uint8_t>(awsSelected_ ^ bit) : bit;
        requestUpdate();
      }
      break;
    case triviaui::ActionAwsSubmit:
      if (view_ == View::AwsOption) awsSubmit();
      break;
    case triviaui::ActionQuit: {
      // Quizmaster keeps no score -- it is a person reading to a room, and
      // score_ belongs to solo. Ending there returns to the menu rather than
      // reporting a total that was never counted, or worse, a stale one left
      // over from an earlier solo round.
      if (view_ == View::Quizmaster) {
        go(View::Menu);
        break;
      }
      // Ending the round IS the summary. The two findings -- "cannot leave the
      // quiz" and "no final score" -- were one omission: with no deliberate way
      // to stop, there was no moment at which a score could be shown, so the
      // only exit (the HOME key) also threw the result away.
      //
      // Reuses the notice machinery, whose ActionMenuRow already returns to the
      // menu from a Notice, rather than adding a fifth View for one screen.
      char body[160];
      if (score_.asked == 0) {
        std::snprintf(body, sizeof(body), "%s", "No questions answered, so nothing to score.");
      } else {
        std::snprintf(body, sizeof(body), "You got %d of %d.", score_.right, score_.asked);
      }
      showNotice("ROUND OVER", body, "BACK TO MENU", triviaui::ActionMenuRow);
      break;
    }
    case triviaui::ActionReveal:
      revealed_ = true;
      requestUpdate();
      break;
    case triviaui::ActionNext:
      // From the HIDDEN notice, put the mode back first: deal() reads view_ to
      // decide whether it needs a question with distractors.
      if (view_ == View::Notice) go(flagReturn_);
      deal();
      break;
    case triviaui::ActionOption:
      if (chosen_ < 0 && value >= 0 && value < trivia::kOptions) {
        chosen_ = value;
        score_.record(value == choices_.correct);
        requestUpdate();
      }
      break;
    case triviaui::ActionFlag:
      // The whole curation loop: a question judged bad at a bar is never served
      // again, and the index is read back off the card into verdicts.tsv.
      if (haveQuestion_) {
        state_.setFlag(current_, trivia::kFlagged);
        LOG_INF("TRIVIA", "Flagged question %u", static_cast<unsigned>(current_));
        // Say what happened. Before this the question simply changed, which is
        // exactly what NEXT does, so a cold tester could not tell the button
        // had any effect at all and stopped pressing it.
        flagReturn_ = view_;
        char body[160];
        std::snprintf(body, sizeof(body),
                      "That question will not come back. %u hidden so far.",
                      static_cast<unsigned>(state_.flaggedCount()));
        showNotice("HIDDEN", body, "NEXT QUESTION", triviaui::ActionNext);
      }
      break;
    default:
      break;
  }
}

void TriviaActivity::loop() {
  // Started from the action handler rather than inside it: the download blocks
  // for minutes and pumps input itself, which must not happen while an action
  // is still being routed.
  if (downloadQueued_) {
    downloadQueued_ = false;
    runPackDownload();
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // Touch only, like the rest of this fork's games. The app never blocks the
  // loop, so it must not call mappedInput.update(): the shell already did.
  if (!input.touchReleased || !interactionsReady_) return;

  const fui::ActionEvent action = interactions_.route(input);
  routeAction(static_cast<int>(action.action), static_cast<int>(action.value));
}

void TriviaActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Faces per view, not per app. The clue is a page of prose and wants the
  // reading serif; the front door is a menu and must look like every other
  // menu in the fork, which is Jersey. proseMenuFaces exists for exactly this
  // shape -- a headline with a sentence under it -- because at the 20px UI cut
  // a sentence runs off the panel and is truncated with an ellipsis Jersey
  // does not carry, so the line simply stops. See ToyboxTheme.h.
  const bool prose = view_ == View::Quizmaster || view_ == View::Solo || view_ == View::Notice ||
                     view_ == View::AwsQuestion || view_ == View::AwsOption ||
                     view_ == View::AwsExplanation || view_ == View::AwsDomains;
  auto target =
      toybox::makeTarget(renderer, prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces());
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::Notice: {
      triviaui::NoticeModel model;
      model.headline = noticeHead_;
      model.body = noticeBody_;
      model.actionLabel = noticeAction_;
      model.action = noticeActionId_;
      triviaui::buildNotice(screen, model);
      break;
    }
    case View::Menu: {
      triviaui::MenuModel model;
      model.selected = selected_;
      model.difficulty = difficulty_;
      model.packCount = pack_.count();
      model.seenCount = state_.seenCount();
      model.englishFallback = I18N.getLanguage() == Language::DE && !germanContent_;
      triviaui::buildMenu(screen, model);
      break;
    }
    case View::AwsMenu: {
      triviaui::AwsMenuModel model;
      model.count = awsPack_.count();
      model.seen = awsState_.seenCount();
      model.enabled = awsDomains_;
      triviaui::buildAwsMenu(screen, model);
      break;
    }
    case View::AwsDomains: {
      triviaui::AwsDomainsModel model;
      model.enabled = awsDomains_;
      triviaui::buildAwsDomains(screen, model);
      break;
    }
    case View::AwsQuestion:
    case View::AwsOption:
    case View::AwsExplanation: {
      awsBuildPage();
      triviaui::AwsPageModel model;
      model.text = awsPageBuffer_;
      model.domain = awsQuestion_.domain();
      model.option = awsOption_;
      model.optionCount = awsQuestion_.optionCount();
      model.correct = awsQuestion_.correctMask();
      model.selected = awsSelected_;
      model.asked = awsScore_.asked;
      model.right = awsScore_.right;
      model.more = awsMore_;
      model.correctAnswer = awsAnswerCorrect_;
      model.hasQuestion = awsHasQuestion_;
      if (view_ == View::AwsQuestion) {
        triviaui::buildAwsQuestion(screen, model);
      } else if (view_ == View::AwsOption) {
        triviaui::buildAwsOption(screen, model);
      } else {
        triviaui::buildAwsExplanation(screen, model);
      }
      break;
    }
    case View::Quizmaster: {
      triviaui::QuestionModel model;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        if (revealed_) model.answer = question_.answer();
      } else {
        model.clue = "No question available. Try another difficulty.";
      }
      triviaui::buildQuestion(screen, model);
      break;
    }
    case View::Solo: {
      triviaui::ChoiceModel model;
      model.asked = score_.asked;
      model.right = score_.right;
      model.chosen = chosen_;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        model.correct = choices_.correct;
        for (int i = 0; i < trivia::kOptions; ++i) model.option[i] = choices_.option[i];
      } else {
        model.clue = "No multiple-choice question available at this difficulty.";
      }
      triviaui::buildChoice(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Trivia");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(flashOnNextPaint_ ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint_ = false;
}
