#include "TriviaActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
// XTRIVIA content belongs on SD. OTA firmware images do not contain trivia.
constexpr const char* kGermanPackUrl =
    "https://github.com/0b-ivan/inx-4x-pro/releases/download/trivia-de-pack/pack-de.dat";
constexpr const char* kGermanPartPath = "/trivia/pack-de.dat.part";

// One byte: the difficulty filter. It lived only in the activity, which is
// deleted on exit, so leaving Trivia and coming back silently put a player who
// had chosen Level 4 back on "Any" with nothing on screen to say so.
constexpr const char* kPrefsPath = "/trivia/prefs";
constexpr const char* kSelectedStudyDeck = "/trivia/study-selected";
// Public GitHub data packs only. Never publish proprietary exam questions.
constexpr const char* kStudyCatalogUrl =
    "https://github.com/0b-ivan/inx-4x-pro/releases/download/trivia-study-packs/catalog.txt";
constexpr const char* kStudyAssetBase =
    "https://github.com/0b-ivan/inx-4x-pro/releases/download/trivia-study-packs/";
constexpr const char* kStudyCatalogTemp = "/trivia/study-catalog.part";
constexpr uint32_t kStudyPathCapacity = 96;
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

void TriviaActivity::scanStudyDecks() {
  deckCatalog_.clear();
  // AWS packs stay in /trivia/aws, and unrelated study decks in /trivia/decks.
  // Include both in one selector without moving private question content.
  for (const String& filename : Storage.listFiles(trivia::kAwsStudyDeckFolder, 160))
    deckCatalog_.addAwsFile(filename.c_str());
  for (const String& filename : Storage.listFiles(trivia::kStudyDeckFolder, 160))
    deckCatalog_.addFile(filename.c_str());
  selectedDeck_ = 0;
  char saved[trivia::kStudySlugMax + 2] = {};
  if (Storage.readFileToBuffer(kSelectedStudyDeck, saved, sizeof(saved)) > 0) {
    const int found = deckCatalog_.find(saved);
    if (found >= 0) selectedDeck_ = static_cast<uint8_t>(found);
  }
  deckPage_ = selectedDeck_ / trivia::kStudyDecksPerPage;
}

bool TriviaActivity::studyPath(const char* ext, char* out, uint32_t capacity) const {
  return deckCatalog_.path(selectedDeck_, ext, out, capacity);
}

void TriviaActivity::loadStudyTopics() {
  // No built-in AWS taxonomy: each certification brings its own topics.
  for (int i = 0; i < trivia::kAwsDomains; ++i)
    std::snprintf(studyTopics_[i], sizeof(studyTopics_[i]), "TOPIC %d", i + 1);
  char path[kStudyPathCapacity] = {};
  if (!studyPath(".topics", path, sizeof(path))) return;
  char raw[256] = {};
  const size_t bytes = Storage.readFileToBuffer(path, raw, sizeof(raw));
  if (bytes == 0 || bytes >= sizeof(raw)) return;
  const char* cursor = raw;
  for (int i = 0; i < trivia::kAwsDomains && *cursor; ++i) {
    const char* end = std::strchr(cursor, '\n');
    if (!end) end = cursor + std::strlen(cursor);
    const size_t length = static_cast<size_t>(end - cursor);
    bool valid = length != 0 && length < sizeof(studyTopics_[i]);
    for (size_t j = 0; valid && j < length; ++j) {
      const unsigned char byte = static_cast<unsigned char>(cursor[j]);
      if (byte < 32 || byte > 126) valid = false;
    }
    if (valid) {
      std::memcpy(studyTopics_[i], cursor, length);
      studyTopics_[i][length] = '\0';
    }
    cursor = *end ? end + 1 : end;
  }
}

bool TriviaActivity::openAwsPack() {
  g_awsPackFile.close();
  g_awsStateFile.close();
  awsInstalled_ = false;
  char packPath[kStudyPathCapacity], statePath[kStudyPathCapacity];
  char idPath[kStudyPathCapacity], prefsPath[kStudyPathCapacity];
  if (!studyPath(".dat", packPath, sizeof(packPath)) ||
      !studyPath(".state", statePath, sizeof(statePath)) ||
      !studyPath(".state.id", idPath, sizeof(idPath)) ||
      !studyPath(".prefs", prefsPath, sizeof(prefsPath))) return false;
  if (!Storage.openFileForRead("TRIVIA", packPath, g_awsPackFile)) return false;
  g_awsPackSource.attach(g_awsPackFile);
  if (!awsPack_.open(g_awsPackSource)) {
    g_awsPackFile.close();
    LOG_ERR("TRIVIA", "Invalid study pack");
    return false;
  }
  bool sameContent = false;
  {
    HalFile stamp = Storage.open(idPath, O_RDONLY);
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
  g_awsStateFile = Storage.open(statePath, O_RDWR);
  if (!sameContent || !g_awsStateFile.isOpen() ||
      static_cast<uint32_t>(g_awsStateFile.size()) != awsPack_.count()) {
    g_awsStateFile.close();
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", statePath, fresh)) return false;
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
    if (!Storage.openFileForWrite("TRIVIA", idPath, stamp)) return false;
    const uint32_t fingerprint = awsPack_.fingerprint();
    const uint8_t id[4] = {
        static_cast<uint8_t>(fingerprint),
        static_cast<uint8_t>(fingerprint >> 8),
        static_cast<uint8_t>(fingerprint >> 16),
        static_cast<uint8_t>(fingerprint >> 24)};
    if (stamp.write(id, sizeof(id)) != sizeof(id)) return false;
    stamp.flush();
    stamp.close();
    g_awsStateFile = Storage.open(statePath, O_RDWR);
  }
  if (!g_awsStateFile.isOpen()) return false;
  g_awsStateSource.attach(g_awsStateFile);
  if (!awsState_.open(g_awsStateSource, awsPack_.count())) return false;
  HalFile prefs = Storage.open(prefsPath, O_RDONLY);
  awsDomains_ = trivia::kAwsAllDomains;
  if (prefs.isOpen() && prefs.size() == 1) {
    uint8_t mask = 0;
    if (prefs.read(&mask, 1) == 1 && !(mask & ~trivia::kAwsAllDomains))
      awsDomains_ = mask;
  }
  awsChooser_.begin(awsPack_, awsState_, rng_);
  loadStudyTopics();
  awsScore_.reset();
  awsHaveLast_ = false;
  awsHasQuestion_ = false;
  awsInstalled_ = true;
  return true;
}

void TriviaActivity::saveAwsDomains() const {
  HalFile prefs;
  char path[kStudyPathCapacity] = {};
  if (!studyPath(".prefs", path, sizeof(path)) ||
      !Storage.openFileForWrite("TRIVIA", path, prefs)) return;
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
  scanStudyDecks();
  // Opening Trivia on an empty card must still expose the GitHub-pack
  // selector; creating state files is deferred until a deck is chosen.
  view_ = View::Menu;
  LOG_INF("TRIVIA", "Available packs: trivia=%u certificates=%u",
          static_cast<unsigned>(haveTrivia ? pack_.count() : 0),
          static_cast<unsigned>(deckCatalog_.count()));

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

void TriviaActivity::onStudyWifiChosen(const bool connected) {
  if (!connected) {
    showNotice("NO WIFI", "Connect to WiFi to get the public GitHub packs.",
               "TRY AGAIN", triviaui::ActionStudyOnline);
    return;
  }
  studyDownloadQueued_ = true;
}

void TriviaActivity::startStudyFetch() {
  if (WiFi.status() == WL_CONNECTED) {
    studyDownloadQueued_ = true;
    return;
  }
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& r) {
                           onStudyWifiChosen(!r.isCancelled);
                         });
}

// Download only a small text catalog from this hardcoded GitHub release.
// Filenames are parsed by the exact same slug validator as the SD catalog;
// no remote URL, path, or unbounded collection is accepted from the network.
void TriviaActivity::runStudyCatalogFetch() {
  if (!Storage.exists("/trivia") && !Storage.mkdir("/trivia")) {
    showNotice("CARD ERROR", "Cannot create /trivia on SD.",
               "RETRY", triviaui::ActionStudyOnline);
    return;
  }
  downloadCancel_ = false;
  showNotice("GITHUB", "Fetching the public study pack list.", nullptr, 0);
  requestUpdateAndWait();
  const auto err = HttpDownloader::downloadToFile(kStudyCatalogUrl, kStudyCatalogTemp,
                                                  nullptr, &downloadCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(kStudyCatalogTemp);
    showNotice("NETWORK ERROR", "GitHub catalog unavailable. Connect to WiFi and retry.",
               "RETRY", triviaui::ActionStudyOnline);
    return;
  }
  char bytes[1024] = {};
  const size_t size = Storage.readFileToBuffer(kStudyCatalogTemp, bytes, sizeof(bytes));
  Storage.remove(kStudyCatalogTemp);
  if (size == 0 || size >= sizeof(bytes)) {
    showNotice("BAD CATALOG", "GitHub list is empty or too large.",
               "RETRY", triviaui::ActionStudyOnline);
    return;
  }
  trivia::StudyDeckCatalog candidate;
  char* cursor = bytes;
  bool valid = true;
  while (*cursor) {
    char* end = std::strchr(cursor, '\n');
    if (!end) { valid = false; break; }
    *end = '\0';
    if (end > cursor && end[-1] == '\r') end[-1] = '\0';
    if (!candidate.addManifestLine(cursor)) { valid = false; break; }
    cursor = end + 1;
  }
  if (!valid || candidate.count() == 0) {
    showNotice("BAD CATALOG", "GitHub returned invalid pack names. No files were changed.",
               "RETRY", triviaui::ActionStudyOnline);
    return;
  }
  remoteCatalog_ = candidate;
  remotePage_ = 0;
  go(View::RemoteStudyDecks);
}

void TriviaActivity::runStudyPackFetch() {
  const auto* entry = remoteCatalog_.at(remoteSelected_);
  if (!entry) { go(View::RemoteStudyDecks); return; }
  const char* directory = entry->aws ? trivia::kAwsStudyDeckFolder : trivia::kStudyDeckFolder;
  if ((!Storage.exists("/trivia") && !Storage.mkdir("/trivia")) ||
      (!Storage.exists(directory) && !Storage.mkdir(directory))) {
    showNotice("CARD ERROR", "Cannot create study pack directory.", "PACKS",
               triviaui::ActionStudyDecks);
    return;
  }
  char pack[96], topics[96], part[112], topicPart[112], backup[112];
  if (!remoteCatalog_.path(remoteSelected_, ".dat", pack, sizeof(pack)) ||
      !remoteCatalog_.path(remoteSelected_, ".topics", topics, sizeof(topics)) ||
      std::snprintf(part, sizeof(part), "%s.part", pack) >= static_cast<int>(sizeof(part)) ||
      std::snprintf(topicPart, sizeof(topicPart), "%s.part", topics) >=
          static_cast<int>(sizeof(topicPart)) ||
      std::snprintf(backup, sizeof(backup), "%s.bak", pack) >= static_cast<int>(sizeof(backup)))
    return;
  const std::string url = std::string(kStudyAssetBase) + entry->slug + ".dat";
  downloadCancel_ = false;
  showNotice("DOWNLOADING", "Downloading the selected public study pack.", nullptr, 0);
  requestUpdateAndWait();
  const auto err = HttpDownloader::downloadToFile(url, part, nullptr, &downloadCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(part);
    showNotice("DOWNLOAD FAILED", "The previous questions were not changed.",
               "PACKS", triviaui::ActionStudyDecks);
    return;
  }
  // Validate before replacing a local pack. Never swap in an HTML error page,
  // an XTRIVIA general-quiz pack, or an incomplete indexed file.
  HalFile file;
  if (!Storage.openFileForRead("TRIVIA", part, file)) {
    Storage.remove(part);
    showNotice("BAD PACK", "Downloaded data cannot be read.", "PACKS",
               triviaui::ActionStudyDecks);
    return;
  }
  FileSource source;
  source.attach(file);
  trivia::AwsPack candidate;
  const bool valid = candidate.open(source) &&
                     candidate.read(0, awsQuestion_) &&
                     candidate.read(candidate.count() - 1, awsQuestion_);
  file.close();
  if (!valid) {
    Storage.remove(part);
    showNotice("BAD PACK", "GitHub file failed format validation.", "PACKS",
               triviaui::ActionStudyDecks);
    return;
  }
  Storage.remove(backup);
  const bool hadOld = Storage.exists(pack);
  if (hadOld && !Storage.rename(pack, backup)) {
    Storage.remove(part);
    showNotice("CARD ERROR", "Could not safeguard your installed pack.",
               "PACKS", triviaui::ActionStudyDecks);
    return;
  }
  if (!Storage.rename(part, pack)) {
    if (hadOld) Storage.rename(backup, pack);
    showNotice("CARD ERROR", "Could not install file; previous pack restored.",
               "PACKS", triviaui::ActionStudyDecks);
    return;
  }
  if (hadOld) Storage.remove(backup);

  // Topic labels are a presentation sidecar. The pack remains usable even
  // if this optional second GitHub asset is unavailable.
  const std::string topicUrl = std::string(kStudyAssetBase) + entry->slug + ".topics";
  if (HttpDownloader::downloadToFile(topicUrl, topicPart) == HttpDownloader::OK) {
    Storage.remove(topics);
    if (!Storage.rename(topicPart, topics)) Storage.remove(topicPart);
  } else {
    Storage.remove(topicPart);
  }

  scanStudyDecks();
  char message[150];
  std::snprintf(message, sizeof(message), "%s installed. Choose it under Study Packs.",
                entry->slug);
  showNotice("PACK READY", message, "PACKS", triviaui::ActionStudyDecks);
}

void TriviaActivity::runPackDownload() {
  // Firmware OTA and Trivia packs are separate. Leave English and German packs
  // and their local seen/flagged state files isolated from one another.
  const bool downloadingGerman = I18N.getLanguage() == Language::DE;
  const char* const partPath = downloadingGerman ? kGermanPartPath : kPartPath;
  const char* const packPath =
      downloadingGerman ? trivia::kGermanPackLocation.packPath : kPackPath;
  const char* const packUrl = downloadingGerman ? kGermanPackUrl : kPackUrl;
  const uint8_t requiredPackFlag = downloadingGerman ? trivia::kPackGermanFlag : 0;
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

  const auto err = HttpDownloader::downloadToFile(packUrl, partPath, progress, &downloadCancel_);
  if (err != HttpDownloader::OK) {
    Storage.remove(partPath);
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

  // Reject damaged or mismatched language packs BEFORE touching the installed
  // destination. German content has XTRIVIA header flag=1, English flag=0.
  {
    HalFile candidate;
    if (!Storage.openFileForRead("TRIVIA", partPath, candidate)) {
      Storage.remove(partPath);
      showNotice("BAD PACK", "Cannot read downloaded data; no questions replaced.",
                 "TRY AGAIN", triviaui::ActionGetPack);
      return;
    }
    FileSource candidateSource;
    candidateSource.attach(candidate);
    trivia::Pack candidatePack;
    const bool valid = candidatePack.open(candidateSource) &&
                       candidatePack.flags() == requiredPackFlag &&
                       candidatePack.count() > 0;
    candidate.close();
    if (!valid) {
      Storage.remove(partPath);
      showNotice("BAD PACK", "Wrong-language or damaged pack; no questions replaced.",
                 "TRY AGAIN", triviaui::ActionGetPack);
      return;
    }
  }
  Storage.remove(packPath);
  if (!Storage.rename(partPath, packPath)) {
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

uint32_t TriviaActivity::awsMissedCount() const {
  uint32_t count = 0;
  for (uint32_t i = 0; i < awsState_.count(); ++i) {
    if (!(awsState_.flags(i) & trivia::kAwsMissed)) continue;
    uint8_t domain = 0;
    if (awsPack_.domainAt(i, domain) &&
        (awsDomains_ & (1u << (domain - 1)))) ++count;
  }
  return count;
}

void TriviaActivity::awsDeal() {
  uint32_t index = 0;
  awsHasQuestion_ = false;
  if (!awsChooser_.next(index, awsDomains_, awsReviewMissed_,
                        awsHaveLast_ ? awsCurrent_ : UINT32_MAX) ||
      !awsPack_.read(index, awsQuestion_)) {
    if (awsReviewMissed_ && awsScore_.asked > 0) {
      go(View::AwsSummary);
      return;
    }
    showNotice("NO STUDY QUESTIONS",
               awsReviewMissed_ ? "No previously missed questions in your enabled domains."
                                : "No matching questions. Enable more topics.",
               "DOMAINS", triviaui::ActionAwsDomains);
    return;
  }
  awsCurrent_ = index;
  awsHaveLast_ = true;
  awsHasQuestion_ = true;
  awsPageStart_ = 0;
  awsOption_ = 0;
  awsSelected_ = 0;
  awsSummaryScroll_ = 0;
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
  if (awsAnswerCorrect_) {
    awsState_.clearFlag(awsCurrent_, trivia::kAwsMissed);
  } else {
    awsState_.setFlag(awsCurrent_, trivia::kAwsMissed);
  }
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
  if (view_ != View::AwsOption && view_ != View::AwsQuestion &&
      view_ != View::AwsExplanation) return;
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
    return;
  }
  // A downward swipe on page one of the question or explanation must not
  // accidentally change option index or restart the question.
  if (view_ != View::AwsOption) return;
  if (awsOption_ > 0) {
    --awsOption_;
    awsPageStart_ = 0;
    requestUpdate();
  } else {
    awsPageStart_ = 0;
    go(View::AwsQuestion);
  }
}

void TriviaActivity::openAwsGlossary() {
  if (view_ != View::AwsQuestion && view_ != View::AwsOption &&
      view_ != View::AwsExplanation) return;
  awsBuildPage();
  awsGlossaryCount_ = trivia::awsGlossaryMatches(awsPageBuffer_, awsGlossaryMatches_);
  if (!awsGlossaryCount_) return;
  awsGlossaryReturn_ = view_;
  go(View::AwsGlossary);
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
      if ((value == 0 || value == 1) && pack_.count() == 0) {
        // A user can install only the private AWS deck. In that case the
        // public Jeopardy pack is absent, but the legacy menu still renders.
        // Do not start an empty round or call an uninitialized chooser.
        showNotice("TRIVIA PACK MISSING",
                   "Install the general questions pack to use Solo or Quizmaster.",
                   "BACK TO MENU", triviaui::ActionAwsHome);
        break;
      }
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
        scanStudyDecks();
        if (!deckCatalog_.count()) {
          showNotice("NO STUDY PACKS",
                     "Place AWS certificates in /trivia/aws or other packs in /trivia/decks.",
                     "BACK", triviaui::ActionAwsHome);
        } else {
          // Even with one certificate, first show the explicit selection.
          // Only ActionStudySelect opens the file and initializes progress.
          go(View::StudyDecks);
        }
      }
      break;
    case triviaui::ActionAwsHome:
      go(View::Menu);
      break;
    case triviaui::ActionStudyDecks:
      scanStudyDecks();
      deckPage_ = selectedDeck_ / trivia::kStudyDecksPerPage;
      go(View::StudyDecks);
      break;
    case triviaui::ActionStudyOnline:
      studyFetch_ = StudyFetch::Catalog;
      startStudyFetch();
      break;
    case triviaui::ActionStudyRemoteNextPage:
      if (view_ == View::RemoteStudyDecks && remoteCatalog_.count()) {
        const uint8_t pages = (remoteCatalog_.count() + trivia::kStudyDecksPerPage - 1) /
                              trivia::kStudyDecksPerPage;
        remotePage_ = static_cast<uint8_t>((remotePage_ + 1) % pages);
        requestUpdate();
      }
      break;
    case triviaui::ActionStudyRemoteSelect:
      if (view_ == View::RemoteStudyDecks &&
          value >= 0 && value < remoteCatalog_.count()) {
        remoteSelected_ = static_cast<uint8_t>(value);
        studyFetch_ = StudyFetch::Pack;
        startStudyFetch();
      }
      break;
    case triviaui::ActionStudyNextPage:
      if (view_ == View::StudyDecks && deckCatalog_.count()) {
        const uint8_t pages = (deckCatalog_.count() + trivia::kStudyDecksPerPage - 1) /
                              trivia::kStudyDecksPerPage;
        deckPage_ = static_cast<uint8_t>((deckPage_ + 1) % pages);
        requestUpdate();
      }
      break;
    case triviaui::ActionStudySelect:
      if (view_ == View::StudyDecks && value >= 0 && value < deckCatalog_.count()) {
        const uint8_t previous = selectedDeck_;
        selectedDeck_ = static_cast<uint8_t>(value);
        if (!openAwsPack()) {
          selectedDeck_ = previous;
          if (deckCatalog_.at(previous)) openAwsPack();
          showNotice("INVALID STUDY PACK", "Pack invalid or missing. Progress preserved.",
                     "PACKS", triviaui::ActionStudyDecks);
          break;
        }
        const auto* selected = deckCatalog_.at(selectedDeck_);
        HalFile saved;
        if (selected && Storage.openFileForWrite("TRIVIA", kSelectedStudyDeck, saved)) {
          saved.write(reinterpret_cast<const uint8_t*>(selected->slug),
                      std::strlen(selected->slug));
          saved.flush();
        }
        go(View::AwsMenu);
      }
      break;
    case triviaui::ActionAwsBackToMenu:
      go(View::AwsMenu);
      break;
    case triviaui::ActionAwsFinish:
      if (awsInstalled_ && (view_ == View::AwsQuestion ||
                            view_ == View::AwsExplanation ||
                            view_ == View::AwsOption)) go(View::AwsSummary);
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
        showNotice("NO DOMAINS", "Enable at least one study topic.",
                   "DOMAINS", triviaui::ActionAwsDomains);
        break;
      }
      awsScore_.reset();
      awsHaveLast_ = false;
      awsReviewMissed_ = action == triviaui::ActionAwsReview;
      awsDeal();
      break;
    case triviaui::ActionAwsPageNext:
      awsNextPage();
      break;
    case triviaui::ActionAwsNextQuestion:
      if (view_ == View::AwsExplanation || view_ == View::AwsSummary) {
        // Completing every missed-question retry must not strand the NEXT
        // button in an empty review queue. Continue with ordinary study.
        if (view_ == View::AwsSummary && awsReviewMissed_ &&
            awsMissedCount() == 0) awsReviewMissed_ = false;
        awsDeal();
      }
      break;
    case triviaui::ActionAwsGlossaryTerm:
      if (view_ == View::AwsGlossary && value >= 0 &&
          value < awsGlossaryCount_) {
        awsGlossarySelected_ = static_cast<uint8_t>(value);
        go(View::AwsGlossaryDetail);
      }
      break;
    case triviaui::ActionAwsGlossaryBack:
      if (view_ == View::AwsGlossaryDetail) go(View::AwsGlossary);
      else if (view_ == View::AwsGlossary) go(awsGlossaryReturn_);
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
      if (view_ == View::AwsOption || view_ == View::AwsQuestion) awsSubmit();
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
  if (studyDownloadQueued_) {
    studyDownloadQueued_ = false;
    const StudyFetch current = studyFetch_;
    studyFetch_ = StudyFetch::None;
    if (current == StudyFetch::Catalog) runStudyCatalogFetch();
    else if (current == StudyFetch::Pack) runStudyPackFetch();
    return;
  }
  if (downloadQueued_) {
    downloadQueued_ = false;
    runPackDownload();
    return;
  }

  // SDK recognizes swipes and long presses independently of ordinary taps.
  // Work only in the text viewport, never on the fixed footer controls.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::None) {
    if (view_ == View::AwsSummary) {
      if (swipe == MappedInputManager::SwipeDir::Up && !awsSummaryScroll_) {
        awsSummaryScroll_ = 1;
        requestUpdate();
      } else if (swipe == MappedInputManager::SwipeDir::Down && awsSummaryScroll_) {
        awsSummaryScroll_ = 0;
        requestUpdate();
      }
      return;
    }
    if (view_ == View::AwsQuestion || view_ == View::AwsOption ||
        view_ == View::AwsExplanation) {
      if (swipe == MappedInputManager::SwipeDir::Up) awsNextPage();
      else if (swipe == MappedInputManager::SwipeDir::Down) awsPreviousPage();
      return;
    }
  }
  if (view_ == View::AwsQuestion || view_ == View::AwsOption ||
      view_ == View::AwsExplanation) {
    int heldX = 0, heldY = 0;
    if (mappedInput.wasScreenLongPress(heldX, heldY)) {
      if (heldX >= 16 && heldX < 464 && heldY >= 150 && heldY < 650)
        openAwsGlossary();
      return;
    }
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
  const bool studyText = view_ == View::AwsQuestion || view_ == View::AwsOption ||
                         view_ == View::AwsExplanation;
  const bool prose = view_ == View::Quizmaster || view_ == View::Solo || view_ == View::Notice ||
                     studyText || view_ == View::AwsDomains ||
                     view_ == View::AwsGlossaryDetail;
  // Reading serif 14px for ordinary questions, 11px for long scenarios.
  // Never shrink below 11px: additional content is paged/swiped instead.
  const bool compact = studyText && std::strlen(awsPageSource()) > 520;
  const toybox::Faces faces = compact
      ? toybox::Faces{toybox::kButtonFontId, toybox::kReadingSmallFontId,
                      toybox::kDisplayFontId}
      : prose ? toybox::readingChromeFaces() : toybox::proseMenuFaces();
  auto target = toybox::makeTarget(renderer, faces);
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
    case View::RemoteStudyDecks: {
      triviaui::StudyDeckListModel model;
      model.german = I18N.getLanguage() == Language::DE;
      model.remote = true;
      model.total = remoteCatalog_.count();
      model.offset = remotePage_ * trivia::kStudyDecksPerPage;
      model.current = remoteSelected_;
      for (uint8_t i = model.offset; i < remoteCatalog_.count() &&
           model.visible < trivia::kStudyDecksPerPage; ++i) {
        const auto* entry = remoteCatalog_.at(i);
        model.indices[model.visible] = i;
        model.names[model.visible] = entry ? entry->slug : "";
        ++model.visible;
      }
      triviaui::buildStudyDeckList(screen, model);
      break;
    }
    case View::StudyDecks: {
      triviaui::StudyDeckListModel model;
      model.german = I18N.getLanguage() == Language::DE;
      model.total = deckCatalog_.count();
      model.offset = deckPage_ * trivia::kStudyDecksPerPage;
      model.current = selectedDeck_;
      for (uint8_t i = model.offset; i < deckCatalog_.count() &&
           model.visible < trivia::kStudyDecksPerPage; ++i) {
        const auto* deck = deckCatalog_.at(i);
        model.indices[model.visible] = i;
        model.names[model.visible] = deck ? deck->slug : "";
        ++model.visible;
      }
      triviaui::buildStudyDeckList(screen, model);
      break;
    }
    case View::AwsMenu: {
      triviaui::AwsMenuModel model;
      model.german = I18N.getLanguage() == Language::DE;
      const auto* activeDeck = deckCatalog_.at(selectedDeck_);
      model.deckName = activeDeck ? activeDeck->slug : "STUDY PACK";
      model.count = awsPack_.count();
      model.seen = awsState_.seenCount();
      model.enabled = awsDomains_;
      triviaui::buildAwsMenu(screen, model);
      break;
    }
    case View::AwsSummary: {
      triviaui::AwsSummaryModel model;
      model.german = I18N.getLanguage() == Language::DE;
      model.asked = awsScore_.asked;
      model.right = awsScore_.right;
      model.seen = awsState_.seenCount();
      model.total = awsPack_.count();
      model.missed = awsMissedCount();
      model.review = awsReviewMissed_;
      model.scroll = awsSummaryScroll_;
      triviaui::buildAwsSummary(screen, model);
      break;
    }
    case View::AwsGlossary:
    case View::AwsGlossaryDetail: {
      triviaui::AwsGlossaryModel model;
      model.german = I18N.getLanguage() == Language::DE;
      model.count = awsGlossaryCount_;
      for (uint8_t i = 0; i < model.count; ++i)
        model.terms[i] = trivia::kAwsGlossary[awsGlossaryMatches_[i]].term;
      if (view_ == View::AwsGlossaryDetail &&
          awsGlossarySelected_ < awsGlossaryCount_) {
        const auto& entry = trivia::kAwsGlossary[awsGlossaryMatches_[awsGlossarySelected_]];
        model.term = entry.term;
        model.description = model.german ? entry.de : entry.en;
      }
      triviaui::buildAwsGlossary(screen, model);
      break;
    }
    case View::AwsDomains: {
      triviaui::AwsDomainsModel model;
      model.german = I18N.getLanguage() == Language::DE;
      for (int i = 0; i < trivia::kAwsDomains; ++i) model.topics[i] = studyTopics_[i];
      model.enabled = awsDomains_;
      triviaui::buildAwsDomains(screen, model);
      break;
    }
    case View::AwsQuestion:
    case View::AwsOption:
    case View::AwsExplanation: {
      awsBuildPage();
      triviaui::AwsPageModel model;
      model.german = I18N.getLanguage() == Language::DE;
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
