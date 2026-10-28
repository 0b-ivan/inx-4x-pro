#include "CardPartyActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "CardPartyTextWrap.h"
#include "CardPartySentence.h"
#ifdef SIMULATOR
#include <cstdlib>
#endif
#include "../../DevMode.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../../network/HttpDownloader.h"
#include "../../util/QrUtils.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxSeed.h"

namespace {
namespace cp = cardparty;
// Released SD packs: active.dat plus any *.dat in the packs folder, chosen on
// the X4. Missing/invalid/unapproved packs fall back to the self-contained
// demo; never accept a .tmp file or draft pack as active.
constexpr const char* kCardPartyDir = "/games/cardparty";
constexpr const char* kCardPartyActivePack = "/games/cardparty/active.dat";
constexpr const char* kCardPartyPackDir = "/games/cardparty/packs";
// The chosen pack's path, or "demo" for the built-in deck.
constexpr const char* kCardPartyDeckChoice = "/games/cardparty/deck.sel";
constexpr const char* kDemoChoice = "demo";
// Public packs: a fixed release of this repository. The list names files
// only; every URL is this base plus catalog.txt or a validated file name.
constexpr const char* kCardPartyPacksBase = "https://github.com/0b-ivan/inx-4x-pro/releases/download/cardparty-packs/";
std::string packsUrl(const char* file) {
#ifdef SIMULATOR
  // SDL QA points the same flow at a local server.
  if (const char* base = std::getenv("CROSSPLAY_SIM_CARDPARTY_PACKS_URL")) return std::string(base) + file;
#endif
  return std::string(kCardPartyPacksBase) + file;
}
// The host's category switches, one record per pack, keyed to its CRC.
constexpr const char* kCardPartySelection = "/games/cardparty/categories.bin";
// Match rules: 'C','R', format 1, rounds, answer s, extension s, Juror s,
// auto s, Rando.
constexpr const char* kCardPartyRules = "/games/cardparty/rules.bin";
constexpr const char* kHistory = "/games/cardparty/history.bin";
constexpr const char* kHistoryPart = "/games/cardparty/history.bin.part";
constexpr const char* kHistoryBackup = "/games/cardparty/history.bin.bak";
constexpr size_t kHistoryMaxBytes = 9 + cp::history::kPlayersStored * 49 +
    cp::history::kWinningLines * 213 + cp::history::kNonFitPairs * 33 + 4;
constexpr uint8_t kRulesBytes = 9;
// What a tap on a rule cycles through.
constexpr uint8_t kRoundOptions[] = {3, 5, 8, 10, 12, 15, 20, 30};
constexpr uint8_t kAnswerOptions[] = {0, 30, 45, 60, 90, 120};
constexpr uint8_t kExtensionOptions[] = {0, 10, 15, 20, 30};
constexpr uint8_t kJurorOptions[] = {0, 30, 60, 90};
constexpr uint8_t kAutoOptions[] = {0, 5, 8, 12};
template <size_t N>
uint8_t nextOption(uint8_t value, const uint8_t (&options)[N]) {
  for (size_t i = 0; i < N; ++i)
    if (options[i] == value) return options[(i + 1) % N];
  return options[0];
}
template <size_t N>
bool isOption(uint8_t value, const uint8_t (&options)[N]) {
  for (uint8_t option : options)
    if (option == value) return true;
  return false;
}
// Rules, menu and players lists.
constexpr int kListTop = 112;
constexpr int kListRow = 56;
constexpr uint8_t kHand = 5;
// Category list on the X4: one row per category, below the hint line.
constexpr int kCategoryTop = 112;
constexpr int kCategoryRow = 36;
// Deck list: the built-in demo, then one row per pack file.
constexpr int kDeckTop = 112;
constexpr int kDeckRow = 56;
// Setup boxes, top and bottom: [01] hotspot, [02] Wi-Fi, [03] deck, then
// [04] categories and [05] rules side by side.
constexpr int kBoxTop[] = {145, 240, 335, 430};
// In-game bottom bar: the main action on the left, [ MENU ] on the right.
constexpr int kMenuButtonWidth = 130;
constexpr int kBoxHeight = 85;
bool hasDatSuffix(const char* name) {
  const size_t n = std::strlen(name);
  if (n < 5) return false;
  const char* ext = name + n - 4;
  return ext[0] == '.' && (ext[1] | 0x20) == 'd' && (ext[2] | 0x20) == 'a' && (ext[3] | 0x20) == 't';
}
class CardPartySdSource final : public cp::sdpack::ByteSource {
 public:
  explicit CardPartySdSource(HalFile& file) : file_(file), length_(static_cast<uint32_t>(file.size())) {}
  uint32_t size() const override { return length_; }
  bool read(uint32_t offset, void* target, uint32_t count) override {
    if (offset > length_ || count > length_ - offset) return false;
    if (!file_.seekSet(offset)) return false;
    return file_.read(target, count) == static_cast<int>(count);
  }

 private:
  HalFile& file_;
  uint32_t length_;
};
constexpr int kGutter = 26;
constexpr int kQr = 160;
const char* phaseLabel(cp::Phase phase) {
  switch (phase) {
    case cp::Phase::Lobby:
      return tr(STR_CAW_WAITING_FOR_PLAYERS);
    case cp::Phase::Playing:
      return tr(STR_CAW_COLLECTING_ANSWERS);
    case cp::Phase::Judging:
      return tr(STR_CAW_JUROR_DECIDES);
    case cp::Phase::Reveal:
      return tr(STR_CAW_ROUND_FINISHED);
    case cp::Phase::Finished:
      return tr(STR_CAW_GAME_OVER);
  }
  return tr(STR_CAW_WAITING);
}
uint32_t visibleFingerprint(const cp::PublicView& v) {
  // Only changes to visible fields trigger e-ink refresh; avoid per-frame
  // repainting for WebSocket housekeeping or repeated unchanged snapshots.
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t x) { h = (h ^ x) * 16777619u; };
  mix(static_cast<uint8_t>(v.phase));
  mix(v.round);
  mix(v.prompt);
  mix(v.submitted);
  mix(v.expected);
  mix(v.czar);
  mix(v.winner);
  mix(v.selected);
  mix(v.aborted ? 1u : 0u);
  for (const auto& p : v.players) {
    mix(p.seated ? 1u : 0u);
    mix(p.connected ? 1u : 0u);
    mix(p.points);
    for (unsigned char c : p.name) mix(c);
  }
  return h;
}
// Pause, block, votes and the clock in 10 s steps: e-ink redraws only then.
uint32_t tableFingerprint(const cp::Table& t, uint32_t now) {
  uint32_t h = 2166136261u;
  auto mix = [&](uint32_t x) { h = (h ^ x) * 16777619u; };
  mix(static_cast<uint8_t>(t.clock()));
  mix(t.clock() == cp::Table::Clock::Next ? 0 : (t.left(now) + 9999) / 10000);
  mix(t.extended() ? 1u : 0u);
  mix(t.pausedBy());
  mix(t.blockedOn());
  mix(t.votes());
  mix(t.ended() ? 1u : 0u);
  return h;
}
// m:ss rounded up to the next 10 s.
void coarseTime(uint32_t ms, char* out, size_t size) {
  const uint32_t tens = (ms + 9999) / 10000;
  std::snprintf(out, size, "%u:%02u", static_cast<unsigned>(tens / 6), static_cast<unsigned>(tens % 6 * 10));
}
}  // namespace

std::unique_ptr<Activity> CardPartyActivity::create(GfxRenderer& renderer, MappedInputManager& input) {
  return makeUniqueNoThrow<CardPartyActivity>(renderer, input);
}
void CardPartyActivity::loadConfiguredDeck() {
  activeDeck_ = cp::demoDeck();
  packLoaded_ = false;
  deckPlayable_ = true;
  deckPath_[0] = 0;
  char chosen[sizeof(deckPath_)] = {};
  if (Storage.exists(kCardPartyDeckChoice)) Storage.readFileToBuffer(kCardPartyDeckChoice, chosen, sizeof(chosen));
  if (std::strcmp(chosen, kDemoChoice) == 0) return;
  if (chosen[0] && loadPack(chosen)) return;
  // Nothing chosen yet, or the chosen file is gone: active.dat, then the
  // first released pack in the folder.
  bool tried = chosen[0] != 0;
  if (Storage.exists(kCardPartyActivePack)) {
    if (loadPack(kCardPartyActivePack)) return;
    tried = true;
  }
  scanDecks();
  for (uint8_t i = 0; i < deckCount_; ++i) {
    if (!decks_[i].readable || !decks_[i].info.released || std::strcmp(decks_[i].path, kCardPartyActivePack) == 0)
      continue;
    if (loadPack(decks_[i].path)) return;
    tried = true;
  }
  if (tried) LOG_ERR("CARDPARTY", "No usable SD pack, using original demo");
}
bool CardPartyActivity::loadPack(const char* path) {
  HalFile file;
  if (!Storage.exists(path) || !Storage.openFileForRead("CARDPARTY", path, file)) return false;
  if (file.size() > cp::sdpack::kMaxPackBytes) {
    LOG_ERR("CARDPARTY", "SD pack %s exceeds memory budget", path);
    return false;
  }
  CardPartySdSource source(file);
  // Draft packs are rejected; a rejected file leaves the loaded pack intact.
  const bool accepted = sdPack_.load(source);
  file.close();
  if (!accepted) {
    LOG_ERR("CARDPARTY", "Rejected invalid/unapproved SD pack %s", path);
    return false;
  }
  packLoaded_ = true;
  activeDeck_ = sdPack_.deck();
  server_.setPackCrc(sdPack_.crc());
  std::snprintf(deckPath_, sizeof(deckPath_), "%s", path);
  LOG_INF("CARDPARTY", "Loaded released SD deck v%u (%u prompts, %u answers, %u categories)",
          static_cast<unsigned>(sdPack_.version()), static_cast<unsigned>(activeDeck_.promptsCount),
          static_cast<unsigned>(activeDeck_.answersCount), static_cast<unsigned>(sdPack_.categoryCount()));
  loadSelection();
  applyCategories();
  return true;
}
void CardPartyActivity::scanDecks() {
  deckCount_ = 0;
  auto add = [this](const char* path, const char* name, HalFile& file) {
    DeckEntry& entry = decks_[deckCount_++];
    std::snprintf(entry.path, sizeof(entry.path), "%s", path);
    std::snprintf(entry.file, sizeof(entry.file), "%s", name);
    CardPartySdSource source(file);
    entry.readable = cp::sdpack::peek(source, entry.info);
  };
  if (Storage.exists(kCardPartyActivePack)) {
    HalFile file;
    if (Storage.openFileForRead("CARDPARTY", kCardPartyActivePack, file)) add(kCardPartyActivePack, "active.dat", file);
  }
  auto dir = Storage.open(kCardPartyPackDir);
  if (dir && dir.isDirectory()) {
    char name[64] = {};
    char path[sizeof(DeckEntry::path)] = {};
    for (auto entry = dir.openNextFile(); entry && deckCount_ < kMaxDecks; entry = dir.openNextFile()) {
      if (entry.isDirectory()) continue;
      // Skips macOS "._" shadow files and names the list cannot hold.
      if (!entry.getName(name, sizeof(name)) || name[0] == '.' || !hasDatSuffix(name) ||
          std::strlen(name) >= sizeof(DeckEntry::file))
        continue;
      const int n = std::snprintf(path, sizeof(path), "%s/%s", kCardPartyPackDir, name);
      if (n < 0 || n >= static_cast<int>(sizeof(path))) continue;
      add(path, name, entry);
    }
  }
  std::sort(decks_, decks_ + deckCount_,
            [](const DeckEntry& a, const DeckEntry& b) { return std::strcmp(a.file, b.file) < 0; });
}
void CardPartyActivity::chooseDeck(int row) {
  if (hosting()) {
    // A running table switches packs only between rounds; hands are dealt anew.
    report_[0] = 0;
    const cp::Phase phase = game_.phase();
    if (phase != cp::Phase::Lobby && phase != cp::Phase::Reveal) {
      std::snprintf(report_, sizeof(report_), "PACKS SWITCH ONLY BETWEEN ROUNDS");
      requestUpdate();
      return;
    }
    bool switched = false;
    {
      RenderLock lock(*this);
      if (row == 0) {
        sdPack_.clear();
        packLoaded_ = false;
        deckPath_[0] = 0;
        activeDeck_ = cp::demoDeck();
        server_.setPackCrc(0xca000001u);
        enabled_ = 0;
        switched = true;
      } else {
        switched = loadPack(decks_[row - 1].path);
      }
      if (switched) {
        game_.setEnabled(enabled_);
        const uint8_t seats = phase == cp::Phase::Lobby ? cp::kPlayers : game_.seatedCount();
        if (!game_.promptsOn() || game_.answersOn() < game_.answersNeeded(seats) || !server_.hostDeckChanged()) {
          // The old pack is gone already: end the match on the new one.
          server_.hostEnd();
          std::snprintf(report_, sizeof(report_), "PACK TOO SMALL FOR THIS TABLE");
        }
      }
    }
    if (!switched) {
      std::snprintf(report_, sizeof(report_), "NOT LOADED: %.40s", decks_[row - 1].file);
      requestUpdate();
      return;
    }
    saveDeckChoice();
    stage_ = Stage::Menu;
    requestUpdate();
    return;
  }
  {
    // The Game holds pointers into the pack until resetGame() reconfigures it.
    RenderLock lock(*this);
    if (row == 0) {
      sdPack_.clear();
      packLoaded_ = false;
      deckPath_[0] = 0;
      activeDeck_ = cp::demoDeck();
      server_.setPackCrc(0xca000001u);
      deckPlayable_ = true;
    } else if (!loadPack(decks_[row - 1].path)) {
      std::snprintf(report_, sizeof(report_), "NOT LOADED: %.40s", decks_[row - 1].file);
      requestUpdate();
      return;
    }
    resetGame();
  }
  saveDeckChoice();
  requestUpdate();
}
void CardPartyActivity::saveDeckChoice() {
  const char* value = deckPath_[0] ? deckPath_ : kDemoChoice;
  const size_t n = std::strlen(value);
  HalFile file;
  if (!Storage.ensureDirectoryExists(kCardPartyDir) ||
      !Storage.openFileForWrite("CARDPARTY", kCardPartyDeckChoice, file) || file.write(value, n) != n)
    LOG_ERR("CARDPARTY", "Could not save deck choice");
}
void CardPartyActivity::loadSelection() {
  enabled_ = cp::categories::defaults(sdPack_);
  uint8_t saved[cp::categories::kSelectionBytes * cp::categories::kSelectionSlots] = {};
  HalFile file;
  if (Storage.exists(kCardPartySelection) && Storage.openFileForRead("CARDPARTY", kCardPartySelection, file)) {
    const int n = file.read(saved, sizeof(saved));
    uint16_t mask = 0;
    if (n > 0 && cp::categories::find(saved, static_cast<size_t>(n), sdPack_.crc(), sdPack_.categoryCount(), mask))
      enabled_ = mask;
    file.close();
  }
  savedEnabled_ = enabled_;
}
void CardPartyActivity::saveSelection() {
  if (enabled_ == savedEnabled_) return;
  uint8_t records[cp::categories::kSelectionBytes * cp::categories::kSelectionSlots] = {};
  size_t len = 0;
  HalFile file;
  if (Storage.exists(kCardPartySelection) && Storage.openFileForRead("CARDPARTY", kCardPartySelection, file)) {
    const int n = file.read(records, sizeof(records));
    if (n > 0) len = static_cast<size_t>(n);
    file.close();
  }
  len = cp::categories::store(records, len, sdPack_.crc(), enabled_);
  if (Storage.openFileForWrite("CARDPARTY", kCardPartySelection, file) && file.write(records, len) == len) {
    savedEnabled_ = enabled_;
  } else {
    LOG_ERR("CARDPARTY", "Could not save category selection");
  }
}
// The game always holds the whole pack; the switches filter when it draws.
void CardPartyActivity::applyCategories() {
  if (!packLoaded_) {
    deckPlayable_ = true;
    return;
  }
  activeDeck_ = sdPack_.deck();
  deckPlayable_ = cp::categories::playable(activeDeck_, enabled_, kHand);
}
void CardPartyActivity::toggleCategory(uint8_t index) {
  const uint16_t before = enabled_;
  enabled_ ^= static_cast<uint16_t>(1u << index);
  if (!hosting()) {
    applyCategories();
    return;
  }
  // A running table keeps its cards; refuse a switch that leaves too few.
  game_.setEnabled(enabled_);
  const uint8_t seats = game_.phase() == cp::Phase::Lobby ? cp::kPlayers : game_.seatedCount();
  if (!game_.promptsOn() || game_.answersOn() < game_.answersNeeded(seats)) {
    enabled_ = before;
    game_.setEnabled(before);
    std::snprintf(report_, sizeof(report_), "TOO FEW CARDS FOR THIS TABLE");
    return;
  }
  report_[0] = 0;
  server_.hostRefresh();
}
void CardPartyActivity::leaveCategories() {
  saveSelection();
  if (hosting()) {
    stage_ = Stage::Menu;
    report_[0] = 0;
  } else {
    resetGame();
  }
  requestUpdate();
}
void CardPartyActivity::loadRules() {
  settings_ = cp::Settings{};
  uint8_t b[kRulesBytes] = {};
  HalFile file;
  if (!Storage.exists(kCardPartyRules) || !Storage.openFileForRead("CARDPARTY", kCardPartyRules, file)) return;
  const bool read = file.read(b, sizeof(b)) == static_cast<int>(sizeof(b));
  file.close();
  if (!read || b[0] != 'C' || b[1] != 'R' || b[2] != 1) return;
  if (!isOption(b[3], kRoundOptions) || !isOption(b[4], kAnswerOptions) || !isOption(b[5], kExtensionOptions) ||
      !isOption(b[6], kJurorOptions) || !isOption(b[7], kAutoOptions) || b[8] > 1)
    return;
  settings_.rounds = b[3];
  settings_.answerSecs = b[4];
  settings_.extensionSecs = b[5];
  settings_.jurorSecs = b[6];
  settings_.autoSecs = b[7];
  settings_.rando = b[8] != 0;
}
void CardPartyActivity::saveRules() {
  const uint8_t b[kRulesBytes] = {'C',
                                  'R',
                                  1,
                                  settings_.rounds,
                                  settings_.answerSecs,
                                  settings_.extensionSecs,
                                  settings_.jurorSecs,
                                  settings_.autoSecs,
                                  static_cast<uint8_t>(settings_.rando ? 1 : 0)};
  HalFile file;
  if (!Storage.ensureDirectoryExists(kCardPartyDir) || !Storage.openFileForWrite("CARDPARTY", kCardPartyRules, file) ||
      file.write(b, sizeof(b)) != sizeof(b))
    LOG_ERR("CARDPARTY", "Could not save match rules");
}
void CardPartyActivity::loadHistory() {
  const auto read = [this](const char* path) {
    if (!Storage.exists(path)) return false;
    HalFile file;
    if (!Storage.openFileForRead("CARDPARTY", path, file)) return false;
    const size_t length = file.size();
    if (length < 13 || length > kHistoryMaxBytes) {
      file.close();
      return false;
    }
    std::vector<uint8_t> bytes(length);
    const bool ok = file.read(bytes.data(), length) == static_cast<int>(length);
    file.close();
    return ok && history_.decode(bytes.data(), bytes.size());
  };
  if (!read(kHistory) && read(kHistoryBackup)) {
    LOG_INF("CARDPARTY", "Recovered player history from backup");
    // Promote the known-good backup before the next snapshot rotation. A
    // corrupt primary must never overwrite the only recoverable version.
    Storage.remove(kHistory);
    Storage.rename(kHistoryBackup, kHistory);
  }
  historySaved_ = history_.revision();
}
void CardPartyActivity::saveHistory() {
  if (historySaved_ == history_.revision()) return;
  if (!Storage.ensureDirectoryExists(kCardPartyDir)) return;
  const std::vector<uint8_t> bytes = history_.encode();
  if (bytes.empty() || bytes.size() > kHistoryMaxBytes) return;
  Storage.remove(kHistoryPart);
  HalFile file;
  if (!Storage.openFileForWrite("CARDPARTY", kHistoryPart, file)) return;
  const bool written = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.close();
  if (!written) {
    Storage.remove(kHistoryPart);
    LOG_ERR("CARDPARTY", "Could not write history snapshot");
    return;
  }
  Storage.remove(kHistoryBackup);
  const bool hadOld = Storage.exists(kHistory);
  if (hadOld && !Storage.rename(kHistory,kHistoryBackup)) {
    Storage.remove(kHistoryPart);
    return;
  }
  if (!Storage.rename(kHistoryPart,kHistory)) {
    if (hadOld) Storage.rename(kHistoryBackup,kHistory);
    Storage.remove(kHistoryPart);
    return;
  }
  // Retain the previous CRC-valid snapshot as a recovery candidate.
  historySaved_ = history_.revision();
}
void CardPartyActivity::cycleRule(uint8_t row) {
  switch (row) {
    case 0:
      if (hosting() && game_.phase() != cp::Phase::Lobby) {
        std::snprintf(report_, sizeof(report_), "ROUNDS ARE FIXED ONCE THE GAME RUNS");
        return;
      }
      settings_.rounds = nextOption(settings_.rounds, kRoundOptions);
      if (hosting()) game_.setRounds(settings_.rounds);
      break;
    case 1:
      settings_.answerSecs = nextOption(settings_.answerSecs, kAnswerOptions);
      break;
    case 2:
      settings_.extensionSecs = nextOption(settings_.extensionSecs, kExtensionOptions);
      break;
    case 3:
      settings_.jurorSecs = nextOption(settings_.jurorSecs, kJurorOptions);
      break;
    case 4:
      settings_.autoSecs = nextOption(settings_.autoSecs, kAutoOptions);
      break;
    case 5:
      settings_.rando = !settings_.rando;
      break;
    default:
      return;
  }
  report_[0] = 0;
  server_.hostSettings(settings_);
}
void CardPartyActivity::leaveRules() {
  saveRules();
  if (hosting()) {
    stage_ = Stage::Menu;
    report_[0] = 0;
  } else {
    resetGame();  // rounds and Rando go into the next configure
  }
  requestUpdate();
}
void CardPartyActivity::resetGame() {
  abortArmed_ = false;
  abortArmedAt_ = 0;
  backArmed_ = false;
  backArmedAt_ = 0;
  kickArmed_ = cp::kNone;
  endArmed_ = false;
  const auto rules = cp::Rules{kHand, settings_.rounds, settings_.rando};
  if (!deckPlayable_) {
    // Switched off too much: stay in setup and say why instead of failing.
    stage_ = Stage::Choice;
    std::snprintf(report_, sizeof(report_), "TOO FEW CARDS: SWITCH ON MORE CATEGORIES");
  } else if (!game_.configure(activeDeck_, rules, toybox::seed())) {
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_), "DECK INVALID");
  } else {
    game_.setEnabled(enabled_);
    stage_ = Stage::Choice;
    report_[0] = 0;
  }
  sessions_.reset();
  fingerprint_ = visibleFingerprint(game_.publicView());
}
void CardPartyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadRules();
  loadHistory();
  server_.setHistory(&history_);
  server_.hostSettings(settings_);
  loadConfiguredDeck();
  server_.setPackCrc(packLoaded_ ? sdPack_.crc() : 0xca000001u);
  resetGame();
#ifdef SIMULATOR
  // SDL fixture mode exercises the actual CardPartyActivity renderer.
  // Real production networking is never enabled in the simulator.
  const char* scenario = std::getenv("CROSSPLAY_SIM_CARDPARTY_STATE");
  if (scenario && std::strcmp(scenario, "categories") == 0) {
    stage_ = Stage::Categories;
  } else if (scenario && std::strcmp(scenario, "decks") == 0) {
    scanDecks();
    stage_ = Stage::Decks;
  } else if (scenario && std::strcmp(scenario, "remote") == 0) {
    // The GitHub list as the device shows it: fetched from a local server
    // when one is set, else a fixed sample. The SDL build skips the Wi-Fi picker.
    scanDecks();
    if (std::getenv("CROSSPLAY_SIM_CARDPARTY_PACKS_URL")) {
      WiFi.mode(WIFI_STA);
      WiFi.begin("SimLAN");
      ownsSta_ = true;
      openRemote();
    } else {
      static constexpr char kSample[] = "Deutsch.dat|Deutsch - alle Karten|40770\nqa.dat|QA Deck|9000\n";
      catalog_.parse(kSample, sizeof(kSample) - 1);
      stage_ = Stage::Remote;
    }
  } else if (scenario && std::strcmp(scenario, "rules") == 0) {
    stage_ = Stage::Rules;
  } else if (scenario && *scenario && std::strcmp(scenario, "setup") != 0) {
    start(cp::web::Server::Mode::Hotspot);
    // Host screens over a running round.
    if (std::strcmp(scenario, "menu") == 0) stage_ = Stage::Menu;
    if (std::strcmp(scenario, "players") == 0) stage_ = Stage::Players;
    if (std::strcmp(scenario, "paused") == 0) server_.hostPause();
  }
#endif
  requestUpdate();
}
void CardPartyActivity::onExit() {
  if (game_.phase() == cp::Phase::Lobby || game_.phase() == cp::Phase::Reveal ||
      game_.phase() == cp::Phase::Finished) saveHistory();
  releaseNetwork();
  Activity::onExit();
}
void CardPartyActivity::releaseNetwork() {
  server_.stop();
  if (ownsSta_) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    ownsSta_ = false;
  }
  if (devModePaused_) {
    devmode::resume();
    devModePaused_ = false;
  }
}
void CardPartyActivity::leave() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}
void CardPartyActivity::start(cp::web::Server::Mode mode) {
  if (!deckPlayable_) {
    requestUpdate();
    return;
  }
  server_.setLanguage(I18N.getLanguage() == Language::DE ? "de" : "en");
  if (!server_.begin(mode)) {
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_),
                  mode == cp::web::Server::Mode::Hotspot ? "HOTSPOT COULD NOT START" : "CONNECT TO WI-FI FIRST");
  } else {
    stage_ = Stage::Hosted;
    fingerprint_ = visibleFingerprint(game_.publicView()) ^ tableFingerprint(server_.table(), millis());
  }
  requestUpdate();
}
void CardPartyActivity::chooseExistingWifi() {
  if (!deckPlayable_) {
    requestUpdate();
    return;
  }
  connectWifi(Stage::Choice, &CardPartyActivity::hostExistingWifi);
}
void CardPartyActivity::hostExistingWifi() { start(cp::web::Server::Mode::ExistingWifi); }
void CardPartyActivity::connectWifi(Stage back, void (CardPartyActivity::*onReady)()) {
  // Already associated (dev mode keeps its last network): go right away.
  if (WiFi.status() == WL_CONNECTED) {
    (this->*onReady)();
    return;
  }
  if (!devModePaused_) {
    devmode::pause();
    devModePaused_ = true;
  }
  WiFi.mode(WIFI_STA);
  ownsSta_ = true;
  auto picker = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!picker) {
    LOG_ERR("CARDPARTY", "OOM: WifiSelectionActivity");
    releaseNetwork();
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_), "OUT OF MEMORY");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(picker), [this, back, onReady](const ActivityResult& result) {
    if (result.isCancelled) {
      releaseNetwork();
      stage_ = back;
      requestUpdate();
      return;
    }
    const auto& wifi = std::get<WifiResult>(result.data);
    if (!wifi.connected) {
      releaseNetwork();
      // Hosting fails to the error screen; a download goes back to its list.
      stage_ = back == Stage::Choice ? Stage::Failed : back;
      std::snprintf(report_, sizeof(report_), "WI-FI NOT CONNECTED");
      requestUpdate();
      return;
    }
    (this->*onReady)();
  });
}
void CardPartyActivity::openRemote() {
  catalog_.clear();
  report_[0] = 0;
  connectWifi(Stage::Decks, &CardPartyActivity::queueCatalog);
}
void CardPartyActivity::queueCatalog() {
  stage_ = Stage::Remote;
  job_ = RemoteJob::Catalog;
  busy_ = "LOADING THE PACK LIST FROM GITHUB";
  requestUpdate();
}
void CardPartyActivity::runRemoteJob() {
  const RemoteJob job = job_;
  job_ = RemoteJob::None;
  requestUpdateAndWait();  // the screen says what is happening first
  if (job == RemoteJob::Catalog)
    fetchCatalog();
  else
    installPack(jobIndex_);
  busy_ = nullptr;
  requestUpdate();
}
void CardPartyActivity::fetchCatalog() {
  char text[cp::catalog::kMaxBytes] = {};
  size_t used = 0;
  bool overflow = false;
  const bool fetched = HttpDownloader::fetchUrl(packsUrl("catalog.txt"), [&](const uint8_t* data, size_t len) {
    if (len > sizeof(text) - used) {
      overflow = true;
      return false;
    }
    std::memcpy(text + used, data, len);
    used += len;
    return true;
  });
  if (!fetched && !overflow) {
    const int status = HttpDownloader::lastStatus();
    if (status == 0)
      std::snprintf(report_, sizeof(report_), "GITHUB NOT REACHABLE. CHECK THE WI-FI AND TRY AGAIN.");
    else
      std::snprintf(report_, sizeof(report_), "NO PACK LIST ON GITHUB (HTTP %d)", status);
  } else if (overflow || !catalog_.parse(text, used)) {
    LOG_ERR("CARDPARTY", "Rejected pack list from GitHub (%u bytes)", static_cast<unsigned>(used));
    std::snprintf(report_, sizeof(report_), "PACK LIST FROM GITHUB IS INVALID");
  } else {
    report_[0] = 0;
  }
}
void CardPartyActivity::installPack(uint8_t index) {
  if (index >= catalog_.count()) return;
  const cp::catalog::Entry& entry = catalog_.at(index);
  char path[sizeof(DeckEntry::path)] = {}, part[sizeof(path) + 8] = {}, backup[sizeof(path) + 8] = {};
  std::snprintf(path, sizeof(path), "%s/%s", kCardPartyPackDir, entry.file);
  std::snprintf(part, sizeof(part), "%s.part", path);
  std::snprintf(backup, sizeof(backup), "%s.bak", path);
  if (!Storage.ensureDirectoryExists(kCardPartyPackDir)) {
    std::snprintf(report_, sizeof(report_), "CANNOT CREATE THE PACKS FOLDER");
    return;
  }
  downloadCancel_ = false;
  const auto limit = [this](size_t done, size_t total) {
    if (done > cp::sdpack::kMaxPackBytes || total > cp::sdpack::kMaxPackBytes) downloadCancel_ = true;
  };
  const std::string url = packsUrl(entry.file);
  if (HttpDownloader::downloadToFile(url, part, limit, &downloadCancel_) != HttpDownloader::OK) {
    Storage.remove(part);
    std::snprintf(report_, sizeof(report_), "DOWNLOAD FAILED: %.40s", entry.title);
    return;
  }
  // Keep an installed copy until the new file has loaded.
  Storage.remove(backup);
  const bool hadOld = Storage.exists(path);
  if ((hadOld && !Storage.rename(path, backup)) || !Storage.rename(part, path)) {
    if (hadOld && !Storage.exists(path)) Storage.rename(backup, path);
    Storage.remove(part);
    std::snprintf(report_, sizeof(report_), "SD CARD ERROR, NOTHING CHANGED");
    return;
  }
  bool loaded = false;
  {
    RenderLock lock(*this);
    loaded = loadPack(path);
    if (loaded) resetGame();
  }
  if (!loaded) {
    Storage.remove(path);
    if (hadOld) Storage.rename(backup, path);
    scanDecks();
    std::snprintf(report_, sizeof(report_), "NOT A VALID PACK: %.40s", entry.title);
    return;
  }
  Storage.remove(backup);
  saveDeckChoice();
  releaseNetwork();
  catalog_.clear();
  scanDecks();
  std::snprintf(report_, sizeof(report_), "INSTALLED: %.60s", entry.title);
}
void CardPartyActivity::leaveRemote() {
  releaseNetwork();
  catalog_.clear();
  busy_ = nullptr;
  scanDecks();
  stage_ = Stage::Decks;
  report_[0] = 0;
  requestUpdate();
}
void CardPartyActivity::handleTap(int x, int y) {
  const int height = renderer.getScreenHeight();
  const int width = renderer.getScreenWidth();
  const bool bottomBar = y >= height - 90;
  const int listRow = y >= kListTop ? (y - kListTop) / kListRow : -1;
  if (stage_ == Stage::Choice) {
    const auto inBox = [y](int i) { return y >= kBoxTop[i] && y < kBoxTop[i] + kBoxHeight; };
    const bool categories = packLoaded_ && sdPack_.categoryCount() > 1;
    if (inBox(0)) {
      start(cp::web::Server::Mode::Hotspot);
    } else if (inBox(1)) {
      chooseExistingWifi();
    } else if (inBox(2)) {
      scanDecks();
      stage_ = Stage::Decks;
      report_[0] = 0;
      requestUpdate();
    } else if (y >= 588 && y < 640) {
      fameReturn_ = Stage::Choice;
      famePage_ = 0;
      stage_ = Stage::Fame;
      requestUpdate();
    } else if (inBox(3)) {
      // [04] left half when the pack has categories, [05] otherwise.
      stage_ = categories && x < width / 2 ? Stage::Categories : Stage::Rules;
      report_[0] = 0;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Decks) {
    const int row = (y - kDeckTop) / kDeckRow;
    if (y >= kDeckTop && row <= deckCount_) {
      chooseDeck(row);
    } else if (y >= kDeckTop && row == deckCount_ + 1 && !hosting()) {
      openRemote();
    } else if (bottomBar) {
      stage_ = hosting() ? Stage::Menu : Stage::Choice;
      report_[0] = 0;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Remote) {
    const int row = (y - kDeckTop) / kDeckRow;
    if (busy_) return;
    if (y >= kDeckTop && row < catalog_.count()) {
      jobIndex_ = static_cast<uint8_t>(row);
      job_ = RemoteJob::Pack;
      busy_ = "DOWNLOADING THE PACK FROM GITHUB";
      report_[0] = 0;
      requestUpdate();
    } else if (bottomBar) {
      leaveRemote();
    }
    return;
  }
  if (stage_ == Stage::Categories) {
    const int row = (y - kCategoryTop) / kCategoryRow;
    if (y >= kCategoryTop && row < sdPack_.categoryCount()) {
      toggleCategory(static_cast<uint8_t>(row));
      requestUpdate();
    } else if (bottomBar) {
      leaveCategories();
    }
    return;
  }
  if (stage_ == Stage::Rules) {
    if (listRow >= 0 && listRow < 6) {
      cycleRule(static_cast<uint8_t>(listRow));
      requestUpdate();
    } else if (bottomBar) {
      leaveRules();
    }
    return;
  }
  if (stage_ == Stage::Menu) {
    if (listRow >= 0 && listRow < 8) {
      menuTap(static_cast<uint8_t>(listRow));
    } else if (bottomBar) {
      stage_ = Stage::Hosted;
      report_[0] = 0;
      endArmed_ = false;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Players) {
    if (listRow >= 0) {
      playersTap(listRow);
    } else if (bottomBar) {
      stage_ = Stage::Menu;
      report_[0] = 0;
      kickArmed_ = cp::kNone;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Fame) {
    if (bottomBar) {
      if (x < width / 3) {
        if (famePage_) --famePage_;
      } else if (x < width * 2 / 3) {
        if (famePage_ < 11) ++famePage_;
      } else {
        stage_ = fameReturn_;
      }
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Failed) {
    if (y >= height - 100) {
      releaseNetwork();
      resetGame();
      requestUpdate();
    }
    return;
  }
  if (stage_ != Stage::Hosted || x < 0 || x > width || !bottomBar) return;
  hostAction(x >= width - kGutter - kMenuButtonWidth);
}
// The in-game bottom bar. The left action follows the phase; the X4 is king,
// so it also lifts any pause and answers a block.
void CardPartyActivity::hostAction(bool menu) {
  const cp::Phase phase = game_.phase();
  const cp::Table& table = server_.table();
  abortArmed_ = abortArmed_ && phase == cp::Phase::Judging;
  backArmed_ = false;
  if (phase == cp::Phase::Finished) {
    if (menu) {
      // [ END ]: back to setup; the phones see the session close.
      releaseNetwork();
      resetGame();
    } else if (server_.hostRematch()) {
      report_[0] = 0;
    } else {
      std::snprintf(report_, sizeof(report_), "ACTION NOT AVAILABLE");
    }
    requestUpdate();
    return;
  }
  if (menu) {
    stage_ = Stage::Menu;
    report_[0] = 0;
    requestUpdate();
    return;
  }
  bool accepted = false;
  if (table.pausedBy() != cp::kNone) {
    accepted = server_.hostUnpause();
  } else if (table.blockedOn() != cp::kNone) {
    accepted = server_.hostContinue();
  } else if (phase == cp::Phase::Judging) {
    const uint32_t now = millis();
    if (!abortArmed_ || static_cast<uint32_t>(now - abortArmedAt_) > 7000u) {
      abortArmed_ = true;
      abortArmedAt_ = now;
      std::snprintf(report_, sizeof(report_), "TAP AGAIN TO ABORT ROUND");
      requestUpdate();
      return;
    }
    abortArmed_ = false;
    accepted = server_.hostAbort();
  } else if (phase == cp::Phase::Lobby) {
    accepted = server_.hostStart();
  } else if (phase == cp::Phase::Playing) {
    accepted = server_.hostClose();
  } else if (phase == cp::Phase::Reveal) {
    accepted = server_.hostNext();
  }
  if (!accepted) {
    std::snprintf(report_, sizeof(report_),
                  phase == cp::Phase::Lobby     ? "NEED 3 CONNECTED PLAYERS"
                  : phase == cp::Phase::Playing ? "NO ANSWERS RECEIVED YET"
                                                : "ACTION NOT AVAILABLE");
  } else {
    report_[0] = 0;
  }
  requestUpdate();
}
// Menu rows: 0 pause, 1 +time, 2 rules, 3 categories, 4 deck, 5 players, 6 end.
void CardPartyActivity::menuTap(uint8_t row) {
  const cp::Table& table = server_.table();
  const cp::Phase phase = game_.phase();
  const bool running = phase == cp::Phase::Playing || phase == cp::Phase::Judging || phase == cp::Phase::Reveal;
  report_[0] = 0;
  if (row != 6) endArmed_ = false;
  switch (row) {
    case 0:
      if (table.pausedBy() != cp::kNone)
        server_.hostUnpause();
      else if (!running || !server_.hostPause())
        std::snprintf(report_, sizeof(report_), "NOTHING TO PAUSE");
      break;
    case 1:
      if (!server_.hostAddTime()) std::snprintf(report_, sizeof(report_), "NO CLOCK RUNNING");
      break;
    case 2:
      stage_ = Stage::Rules;
      break;
    case 3:
      if (packLoaded_ && sdPack_.categoryCount() > 1)
        stage_ = Stage::Categories;
      else
        std::snprintf(report_, sizeof(report_), "THIS DECK HAS NO CATEGORIES");
      break;
    case 4:
      scanDecks();
      stage_ = Stage::Decks;
      break;
    case 5:
      kickArmed_ = cp::kNone;
      stage_ = Stage::Players;
      break;
    case 7:
      fameReturn_ = Stage::Menu;
      famePage_ = 0;
      stage_ = Stage::Fame;
      break;
    case 6: {
      const uint32_t now = millis();
      if (!running) {
        std::snprintf(report_, sizeof(report_), "NO MATCH RUNNING");
      } else if (!endArmed_ || static_cast<uint32_t>(now - endArmedAt_) > 7000u) {
        endArmed_ = true;
        endArmedAt_ = now;
        std::snprintf(report_, sizeof(report_), "TAP AGAIN TO END THE GAME FOR EVERYONE");
      } else {
        endArmed_ = false;
        server_.hostEnd();
        stage_ = Stage::Hosted;
      }
      break;
    }
    default:
      break;
  }
  requestUpdate();
}
// Players list: an optional "go on without" row first while blocked, then one
// row per seat. Removing takes a second tap.
void CardPartyActivity::playersTap(int row) {
  const cp::Table& table = server_.table();
  const cp::PublicView v = game_.publicView();
  if (table.blockedOn() != cp::kNone) {
    if (row == 0) {
      server_.hostContinue();
      kickArmed_ = cp::kNone;
      requestUpdate();
      return;
    }
    --row;
  }
  uint8_t seat = cp::kNone;
  for (uint8_t i = 0, n = 0; i < cp::kPlayers; ++i)
    if (v.players[i].seated && n++ == row) seat = i;
  if (seat == cp::kNone) return;
  const uint32_t now = millis();
  if (kickArmed_ != seat || static_cast<uint32_t>(now - kickArmedAt_) > 7000u) {
    kickArmed_ = seat;
    kickArmedAt_ = now;
    std::snprintf(report_, sizeof(report_), "TAP AGAIN TO REMOVE %.40s", v.players[seat].name);
  } else {
    kickArmed_ = cp::kNone;
    if (server_.hostKick(seat))
      std::snprintf(report_, sizeof(report_), "REMOVED %.40s", v.players[seat].name);
    else
      std::snprintf(report_, sizeof(report_), "ACTION NOT AVAILABLE");
    if (game_.phase() == cp::Phase::Finished) stage_ = Stage::Hosted;
  }
  requestUpdate();
}
void CardPartyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (stage_ == Stage::Choice) {
      leave();
      return;
    }
    if (stage_ == Stage::Categories) {
      leaveCategories();
      return;
    }
    if (stage_ == Stage::Rules) {
      leaveRules();
      return;
    }
    if (stage_ == Stage::Decks || stage_ == Stage::Players) {
      stage_ = hosting() ? Stage::Menu : Stage::Choice;
      report_[0] = 0;
      kickArmed_ = cp::kNone;
      requestUpdate();
      return;
    }
    if (stage_ == Stage::Fame) {
      stage_ = fameReturn_;
      requestUpdate();
      return;
    }
    if (stage_ == Stage::Menu) {
      stage_ = Stage::Hosted;
      report_[0] = 0;
      endArmed_ = false;
      requestUpdate();
      return;
    }
    if (stage_ == Stage::Remote) {
      leaveRemote();
      return;
    }
    // One stray press must not end a party for everyone at the table.
    if (stage_ == Stage::Hosted && matchInProgress()) {
      const uint32_t now = millis();
      if (!backArmed_ || static_cast<uint32_t>(now - backArmedAt_) > 7000u) {
        backArmed_ = true;
        backArmedAt_ = now;
        std::snprintf(report_, sizeof(report_), "PRESS BACK AGAIN TO END SESSION");
        requestUpdate();
        return;
      }
    }
    releaseNetwork();
    resetGame();
    requestUpdate();
    return;
  }
  // The websocket callback is allowed to mutate Game. Render task reads
  // snapshots, so serialize the network pump through the Activity render lock.
  if (server_.running()) {
    RenderLock lock(*this);
    server_.loop();
    const uint32_t next = visibleFingerprint(game_.publicView()) ^ tableFingerprint(server_.table(), millis());
    if (next != fingerprint_) {
      fingerprint_ = next;
      requestUpdate();
    }
  }
  // SD writes belong to the activity loop, never to a WebSocket callback.
  // A snapshot is produced only in lobby or between rounds / after the game.
  if (historySaved_ != history_.revision() &&
      (game_.phase() == cp::Phase::Lobby || game_.phase() == cp::Phase::Reveal ||
       game_.phase() == cp::Phase::Finished))
    saveHistory();
  if (job_ != RemoteJob::None) {
    runRemoteJob();
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) handleTap(x, y);
}
void CardPartyActivity::drawBase(const char* section) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  renderer.drawText(UI_10_FONT_ID, kGutter, 20, "C / A / W", true);
  renderer.drawText(SMALL_FONT_ID, width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, section), 25, section, true);
  renderer.fillRect(kGutter, 63, width - 2 * kGutter, 2, true);
}
void CardPartyActivity::drawChoice() {
  drawBase(tr(STR_CAW_SETUP));
  renderer.drawText(UI_10_FONT_ID, kGutter, 92, "CARDS AGAINST WHATEVER", true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 118, tr(STR_CAW_X4_GAME_MASTER_PHONES_CARDS), true);
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  const auto box = [&](int i, const char* title, const char* detail) {
    renderer.drawRect(kGutter, kBoxTop[i], textWidth, kBoxHeight, 2, true);
    renderer.drawText(UI_10_FONT_ID, kGutter + 20, kBoxTop[i] + 21, title, true);
    drawWrapped(SMALL_FONT_ID, detail, kGutter + 20, kBoxTop[i] + 53, textWidth - 40, 22, 1);
  };
  box(0, tr(STR_CAW_01_CREATE_HOTSPOT), tr(STR_CAW_LOCAL_PASSWORD_PROTECTED));
  box(1, tr(STR_CAW_02_EXISTING_WI_FI), tr(STR_CAW_PICK_A_NETWORK_SAME_AS_PLAYERS));
  box(2, tr(STR_CAW_03_DECK), packLoaded_ ? sdPack_.title() : tr(STR_CAW_BUILT_IN_DEMO_TAP_TO_CHOOSE_A_PACK));
  // A single category has nothing to switch: off would only empty the deck.
  char rules[64] = {};
  if (packLoaded_ && sdPack_.categoryCount() > 1) {
    int on = 0;
    for (uint8_t i = 0; i < sdPack_.categoryCount(); ++i)
      if (enabled_ & (1u << i)) ++on;
    const int half = (textWidth - 12) / 2;
    const auto halfBox = [&](int x, const char* title, const char* detail) {
      renderer.drawRect(x, kBoxTop[3], half, kBoxHeight, 2, true);
      renderer.drawText(UI_10_FONT_ID, x + 14, kBoxTop[3] + 21, title, true);
      drawWrapped(SMALL_FONT_ID, detail, x + 14, kBoxTop[3] + 53, half - 28, 22, 1);
    };
    char summary[48] = {};
    std::snprintf(summary, sizeof(summary), tr(STR_CAW_FORMAT_OF_FORMAT_ON), on, static_cast<unsigned>(sdPack_.categoryCount()));
    halfBox(kGutter, tr(STR_CAW_04_CATEGORIES), summary);
    std::snprintf(rules, sizeof(rules), tr(STR_CAW_FORMAT_ROUNDS_FORMAT), settings_.rounds,
                  settings_.answerSecs ? tr(STR_CAW_TIMER) : tr(STR_CAW_NO_TIMER));
    halfBox(kGutter + half + 12, tr(STR_CAW_05_RULES), rules);
  } else {
    std::snprintf(rules, sizeof(rules), tr(STR_CAW_FORMAT_ROUNDS_FORMAT_AUTO_FORMAT), settings_.rounds,
                  settings_.answerSecs ? tr(STR_CAW_TIMER) : tr(STR_CAW_NO_TIMER), settings_.autoSecs ? tr(STR_CAW_ON) : tr(STR_CAW_OFF));
    box(3, tr(STR_CAW_05_RULES), rules);
  }
  if (report_[0]) drawWrapped(SMALL_FONT_ID, report_, kGutter, 535, textWidth, 24, 2);
  if (packLoaded_) {
    // Attribution for the loaded pack, as its license requires.
    char line[420] = {};
    std::snprintf(line, sizeof(line), "%s / %s", sdPack_.rightsHolder(), sdPack_.license());
    drawWrapped(SMALL_FONT_ID, line, kGutter, renderer.getScreenHeight() - 138, textWidth, 22, 3);
  }
  // Setup's Fame entry is below the primary setup controls.
  renderer.drawText(UI_10_FONT_ID, kGutter, 594, tr(STR_CAW_FAME_OPEN), true);
  renderer.drawText(SMALL_FONT_ID, kGutter, renderer.getScreenHeight() - 35, tr(STR_CAW_BACK_CANCEL), true);
}
void CardPartyActivity::drawDecks() {
  drawBase(tr(STR_CAW_DECK));
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  renderer.drawText(SMALL_FONT_ID, kGutter, 82, tr(STR_CAW_TAP_TO_LOAD_SD_GAMES_CARDPARTY_PACKS), true);
  for (int row = 0; row <= deckCount_; ++row) {
    const int y = kDeckTop + row * kDeckRow;
    const DeckEntry* entry = row ? &decks_[row - 1] : nullptr;
    const bool current = entry ? packLoaded_ && std::strcmp(entry->path, deckPath_) == 0 : !packLoaded_;
    const char* title = !entry                                    ? tr(STR_CAW_BUILT_IN_DEMO)
                        : entry->readable && entry->info.title[0] ? entry->info.title
                                                                  : entry->file;
    char line[96] = {};
    std::snprintf(line, sizeof(line), "%s %s", current ? "[x]" : "[  ]", title);
    drawWrapped(UI_10_FONT_ID, line, kGutter, y + 4, textWidth, 26, 1);
    // Status first, file name last: a long name is what gets cut.
    if (!entry)
      std::snprintf(line, sizeof(line), "%s", tr(STR_CAW_ENGLISH_ALWAYS_AVAILABLE));
    else if (!entry->readable)
      std::snprintf(line, sizeof(line), tr(STR_CAW_NOT_A_CAW_PACK_FORMAT), entry->file);
    else if (!entry->info.released)
      std::snprintf(line, sizeof(line), tr(STR_CAW_DRAFT_NOT_PLAYABLE_FORMAT), entry->file);
    else
      std::snprintf(line, sizeof(line), tr(STR_CAW_FORMAT_PROMPTS_FORMAT_ANSWERS_FORMAT), static_cast<unsigned>(entry->info.prompts),
                    static_cast<unsigned>(entry->info.answers), entry->file);
    drawWrapped(SMALL_FONT_ID, line, kGutter + 34, y + 30, textWidth - 34, 22, 1);
    renderer.fillRect(kGutter, y + kDeckRow - 2, textWidth, 1, true);
  }
  const int download = kDeckTop + (deckCount_ + 1) * kDeckRow;
  if (!hosting()) {  // the radio belongs to the party while it runs
    renderer.drawText(UI_10_FONT_ID, kGutter, download + 4, tr(STR_CAW_DOWNLOAD_FROM_GITHUB), true);
    renderer.drawText(SMALL_FONT_ID, kGutter + 34, download + 30, tr(STR_CAW_NEEDS_WI_FI_PUBLIC_CAW_PACKS), true);
    renderer.fillRect(kGutter, download + kDeckRow - 2, textWidth, 1, true);
  }
  if (deckCount_ == 0)
    drawWrapped(SMALL_FONT_ID, tr(STR_CAW_NO_PACKS_ON_THE_CARD_YET_DOWNLOAD_ONE_OR_COPY_DAT_FI),
                kGutter, download + kDeckRow + 16, textWidth, 24, 3);
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_DONE), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawRemote() {
  drawBase("GITHUB");
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  if (busy_) {
    renderer.drawText(UI_10_FONT_ID, kGutter, 112, tr(STR_CAW_PLEASE_WAIT), true);
    drawWrapped(SMALL_FONT_ID, busy_, kGutter, 150, textWidth, 24, 3);
    return;
  }
  if (catalog_.count()) renderer.drawText(SMALL_FONT_ID, kGutter, 82, tr(STR_CAW_TAP_TO_DOWNLOAD_AND_PLAY), true);
  for (uint8_t row = 0; row < catalog_.count(); ++row) {
    const cp::catalog::Entry& entry = catalog_.at(row);
    const int y = kDeckTop + row * kDeckRow;
    drawWrapped(UI_10_FONT_ID, entry.title, kGutter, y + 4, textWidth, 26, 1);
    bool onCard = false;
    for (uint8_t i = 0; i < deckCount_; ++i)
      if (std::strcmp(decks_[i].file, entry.file) == 0) onCard = true;
    char line[96] = {};
    std::snprintf(line, sizeof(line), "%u KB / %s / %s", static_cast<unsigned>((entry.bytes + 1023) / 1024),
                  onCard ? tr(STR_CAW_ON_CARD_TAP_TO_UPDATE) : tr(STR_CAW_NEW), entry.file);
    drawWrapped(SMALL_FONT_ID, line, kGutter + 34, y + 30, textWidth - 34, 22, 1);
    renderer.fillRect(kGutter, y + kDeckRow - 2, textWidth, 1, true);
  }
  if (!catalog_.count())
    drawWrapped(SMALL_FONT_ID, report_[0] ? report_ : tr(STR_CAW_NO_PACKS_LISTED), kGutter, kDeckTop + 16, textWidth, 24, 3);
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_DONE), true);
  if (report_[0] && catalog_.count()) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawCategories() {
  drawBase(tr(STR_CAW_CATEGORIES));
  const int width = renderer.getScreenWidth();
  renderer.drawText(SMALL_FONT_ID, kGutter, 82, tr(STR_CAW_TAP_TO_SWITCH_18_STARTS_OFF), true);
  for (uint8_t i = 0; i < sdPack_.categoryCount(); ++i) {
    const cp::sdpack::Category& c = sdPack_.category(i);
    const int y = kCategoryTop + i * kCategoryRow;
    const bool on = (enabled_ & (1u << i)) != 0;
    char label[48] = {};
    std::snprintf(label, sizeof(label), "%s %s", on ? "[x]" : "[  ]", c.name);
    renderer.drawText(UI_10_FONT_ID, kGutter, y + 4, label, true);
    char count[24] = {};
    std::snprintf(
        count, sizeof(count), "%s%u", c.adult ? "18+  " : "",
        static_cast<unsigned>(cp::categories::cardsIn(sdPack_, i, false) + cp::categories::cardsIn(sdPack_, i, true)));
    renderer.drawText(SMALL_FONT_ID, width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, count), y + 8, count, true);
    renderer.fillRect(kGutter, y + kCategoryRow - 2, width - 2 * kGutter, 1, true);
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, width - 2 * kGutter, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_DONE), true);
  char status[sizeof(report_)] = {};
  const unsigned prompts = cp::categories::promptsIn(activeDeck_, enabled_);
  const unsigned answers = cp::categories::answersIn(activeDeck_, enabled_);
  if (report_[0])
    std::snprintf(status, sizeof(status), "%s", report_);
  else if (deckPlayable_ || hosting())
    std::snprintf(status, sizeof(status), tr(STR_CAW_FORMAT_PROMPTS_FORMAT_ANSWERS_IN_PLAY), prompts, answers);
  else
    std::snprintf(status, sizeof(status), tr(STR_CAW_TOO_FEW_CARDS_FORMAT_OF_FORMAT_ANSWERS), answers,
                  static_cast<unsigned>(cp::categories::requiredAnswers(activeDeck_, enabled_, kHand)));
  renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, status, true);
}
void CardPartyActivity::drawLobby() {
  drawBase(tr(STR_CAW_JOIN_SESSION));
  const int width = renderer.getScreenWidth();
  renderer.drawText(UI_10_FONT_ID, kGutter, 90, tr(STR_CAW_WAITING_FOR_PLAYERS), true);
  int connected = 0;
  const cp::PublicView view = game_.publicView();
  for (const auto& p : view.players)
    if (p.seated && p.connected) ++connected;
  char stats[64] = {};
  std::snprintf(stats, sizeof(stats), tr(STR_CAW_CONNECTED_FORMAT_8_REQUIRED_3), connected);
  renderer.drawText(SMALL_FONT_ID, kGutter, 128, stats, true);

  // 2 QR codes in a WPA2 hotspot: join the private Wi-Fi first, then open
  // the actual game URL in a regular browser (not captive-portal mini UI).
  if (server_.hotspot()) {
    char wifiQr[128] = {};
    std::snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", server_.ssid(), server_.password());
    QrUtils::drawQrCode(renderer, Rect{kGutter, 157, kQr, kQr}, wifiQr);
    renderer.drawText(SMALL_FONT_ID, kGutter, 323, tr(STR_CAW_01_JOIN_WI_FI), true);
    renderer.drawText(SMALL_FONT_ID, kGutter, 347, server_.password(), true);
    QrUtils::drawQrCode(renderer, Rect{kGutter + kQr + 32, 157, kQr, kQr}, server_.url());
    renderer.drawText(SMALL_FONT_ID, kGutter + kQr + 32, 323, tr(STR_CAW_02_OPEN_GAME), true);
  } else {
    QrUtils::drawQrCode(renderer, Rect{kGutter, 157, kQr, kQr}, server_.url());
    renderer.drawText(SMALL_FONT_ID, kGutter, 323, tr(STR_CAW_OPEN_GAME), true);
  }
  // Network, URL and roster as plain text too, so joining works without a camera.
  renderer.drawText(SMALL_FONT_ID, kGutter, 385, server_.ssid(), true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 409, server_.url(), true);
  renderer.fillRect(kGutter, 440, width - 2 * kGutter, 1, true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 452, tr(STR_CAW_PLAYERS), true);
  int y = 482;
  for (const auto& p : view.players) {
    if (!p.seated) continue;
    renderer.drawText(UI_10_FONT_ID, kGutter, y, p.name, true);
    if (!p.connected)
      renderer.drawText(SMALL_FONT_ID, width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, tr(STR_CAW_OFFLINE)), y + 4,
                        tr(STR_CAW_OFFLINE), true);
    y += 26;
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, width - 2 * kGutter, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, connected >= 3 ? tr(STR_CAW_START_GAME) : tr(STR_CAW_WAITING_FOR_3), true);
  renderer.drawText(UI_10_FONT_ID, width - kGutter - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_CAW_MENU)), bottom + 8,
                    tr(STR_CAW_MENU), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
  drawDiagnostics();
}
// Greedy word wrap by measured width; returns the y below the last row.
// Returns the y below the last row. With draw=false it only counts rows, so a
// caller can pick the largest font that fits.
int CardPartyActivity::drawWrapped(int fontId, const char* text, int x, int y, int maxWidth, int lineStep, int maxRows,
                                   bool draw, bool black) {
  struct Context {
    GfxRenderer& renderer;
    int font;
  } context{renderer, fontId};
  const auto measure = [](void* raw, const char* line) {
    const auto& c = *static_cast<Context*>(raw);
    return c.renderer.getTextWidth(c.font, line);
  };
  char line[160] = {};
  int row = 0;
  while (row < maxRows && cp::nextTextLine(text, line, sizeof(line), maxWidth, measure, &context)) {
    if (draw && row == maxRows - 1 && *text) cp::markTextTruncated(line, sizeof(line), maxWidth, measure, &context);
    if (draw) renderer.drawText(fontId, x, y + row * lineStep, line, black);
    ++row;
  }
  return y + row * lineStep;
}
void CardPartyActivity::drawRichSentence(const char* prompt, const char* const* answers,
                                             uint8_t pick, int x, int y, int width) {
  // Mixed normal/inverted words in one sentence. Each word respects measured
  // width, including UTF-8 card text; answer spans get a black reverse field.
  constexpr int kFont = UI_10_FONT_ID;
  constexpr int kLine = 30, kRows = 4;
  int row = 0, usedWidth = 0;
  const int spaceWidth = std::max(8, renderer.getTextWidth(kFont, " "));
  auto drawRun = [&](const char* p, size_t length, bool highlight) {
    size_t pos = 0;
    while (pos < length && row < kRows) {
      if (p[pos] == ' ' || p[pos] == '\n' || p[pos] == '\t') {
        if (p[pos] == '\n' || usedWidth + spaceWidth > width) {
          ++row;
          usedWidth = 0;
        } else if (usedWidth) {
          usedWidth += spaceWidth;
        }
        ++pos;
        continue;
      }
      size_t lengthOfWord = 0;
      while (pos + lengthOfWord < length &&
             p[pos + lengthOfWord] != ' ' && p[pos + lengthOfWord] != '\n' &&
             p[pos + lengthOfWord] != '\t')
        ++lengthOfWord;
      char word[160] = {};
      const size_t copy = std::min(lengthOfWord, sizeof(word) - 1);
      std::memcpy(word, p + pos, copy);
      // Do not leave a truncated UTF-8 character at the end of the token.
      size_t safe = copy;
      if (safe < lengthOfWord)
        while (safe && (static_cast<unsigned char>(p[pos + safe]) & 0xc0u) == 0x80u) --safe;
      word[safe] = 0;
      const int wordWidth = renderer.getTextWidth(kFont, word);
      if (usedWidth && usedWidth + wordWidth > width) {
        ++row;
        usedWidth = 0;
      }
      if (row >= kRows) break;
      if (highlight) {
        renderer.fillRect(x + usedWidth, y + row * kLine - 2, std::min(wordWidth + 3, width - usedWidth), 26, true);
        renderer.drawText(kFont, x + usedWidth + 1, y + row * kLine, word, false);
      } else {
        renderer.drawText(kFont, x + usedWidth, y + row * kLine, word, true);
      }
      usedWidth += wordWidth + (highlight ? 3 : 0);
      pos += lengthOfWord;
    }
  };
  if (!prompt) return;
  uint8_t used = 0;
  const char* p = prompt;
  while (*p && row < kRows) {
    size_t blankBytes = 0;
    if (cp::sentence::blank(p, blankBytes)) {
      if (answers && used < pick && answers[used]) {
        const char* card = answers[used++];
        drawRun(card, std::strlen(card), true);
      } else {
        drawRun(p, blankBytes, false);
      }
      p += blankBytes;
    } else {
      const char* first = p;
      do { ++p; } while (*p && !cp::sentence::blank(p, blankBytes));
      drawRun(first, static_cast<size_t>(p - first), false);
    }
  }
  while (answers && used < pick && row < kRows) {
    drawRun(" ",1,false);
    const char* answer = answers[used++];
    if (answer) drawRun(answer,std::strlen(answer),true);
  }
}
void CardPartyActivity::drawGame() {
  const cp::PublicView v = game_.publicView();
  if (v.phase == cp::Phase::Finished) {
    drawFinal();
    return;
  }
  const cp::Table& table = server_.table();
  const uint8_t pausedBy = table.pausedBy(), blockedOn = table.blockedOn();
  drawBase(pausedBy != cp::kNone ? tr(STR_CAW_PAUSED) : blockedOn != cp::kNone ? tr(STR_CAW_WAITING) : phaseLabel(v.phase));
  const int width = renderer.getScreenWidth();
  char status[96] = {};
  std::snprintf(status, sizeof(status), tr(STR_CAW_ROUND_FORMAT_FORMAT), v.round, game_.rules().rounds);
  renderer.drawText(SMALL_FONT_ID, kGutter, 86, status, true);
  // Clock top right, in 10 s steps: e-ink does not tick every second.
  if (table.clock() != cp::Table::Clock::None) {
    char left[16] = {};
    coarseTime(table.left(millis()), left, sizeof(left));
    std::snprintf(status, sizeof(status), "%s %s%s",
                  table.clock() == cp::Table::Clock::Answers ? tr(STR_CAW_TIME)
                  : table.clock() == cp::Table::Clock::Juror ? tr(STR_CAW_JUROR)
                                                             : tr(STR_CAW_NEXT),
                  left, table.extended() ? " +" : "");
    renderer.drawText(UI_10_FONT_ID, width - kGutter - renderer.getTextWidth(UI_10_FONT_ID, status), 82, status, true);
  }
  const char* czar = v.czar < cp::kPlayers ? v.players[v.czar].name : "-";
  std::snprintf(status, sizeof(status), tr(STR_CAW_JUROR_FORMAT), czar);
  renderer.drawText(SMALL_FONT_ID, kGutter, 110, status, true);
  if (pausedBy != cp::kNone || blockedOn != cp::kNone) {
    // Inverted bar: who stopped the table, readable across the room.
    char banner[64] = {};
    if (pausedBy != cp::kNone)
      std::snprintf(banner, sizeof(banner), tr(STR_CAW_PAUSED_BY_FORMAT),
                    pausedBy == cp::kHost ? tr(STR_CAW_THE_HOST) : v.players[pausedBy].name);
    else
      std::snprintf(banner, sizeof(banner), tr(STR_CAW_WAITING_FOR_FORMAT), v.players[blockedOn].name);
    renderer.fillRect(kGutter, 136, width - 2 * kGutter, 36, true);
    drawWrapped(UI_10_FONT_ID, banner, kGutter + 10, 142, width - 2 * kGutter - 20, 26, 1, true, false);
  } else {
    renderer.fillRect(kGutter, 140, width - 2 * kGutter, 1, true);
    renderer.drawText(SMALL_FONT_ID, kGutter, 156, tr(STR_CAW_PROMPT), true);
  }
  const cp::Deck& deck = activeDeck_;
  const int textWidth = width - 2 * kGutter;
  if (v.prompt < deck.promptsCount) {
    // Read from across the table: the largest cut in which every word and
    // every row fits. The Toybox cuts are ASCII-only, so any UTF-8 prompt
    // goes straight to the UI font.
    const char* prompt = deck.prompts[v.prompt].text;
    constexpr int kPromptTop = 186, kPromptBottom = 470;
    bool ascii = true;
    for (const char* c = prompt; *c; ++c)
      if (static_cast<unsigned char>(*c) >= 0x80) {
        ascii = false;
        break;
      }
    int font = UI_10_FONT_ID;
    int step = 34;
    for (const int candidate : {toybox::kDisplayFontId, toybox::kUiFontId}) {
      if (!ascii) break;
      const int lineStep = renderer.getLineHeight(candidate) + 6;
      const int rows = (kPromptBottom - kPromptTop) / lineStep;
      if (drawWrapped(candidate, prompt, kGutter, kPromptTop, textWidth, lineStep, rows + 1, false) >
          kPromptTop + rows * lineStep)
        continue;
      bool wordsFit = true;
      char word[64] = {};
      for (const char* c = prompt; *c && wordsFit;) {
        while (*c == ' ') ++c;
        size_t n = 0;
        while (c[n] && c[n] != ' ') ++n;
        const size_t copy = std::min(n, sizeof(word) - 1);
        std::memcpy(word, c, copy);
        word[copy] = 0;
        wordsFit = renderer.getTextWidth(candidate, word) <= textWidth;
        c += n;
      }
      if (!wordsFit) continue;
      font = candidate;
      step = lineStep;
      break;
    }
    drawWrapped(font, prompt, kGutter, kPromptTop, textWidth, step, (kPromptBottom - kPromptTop) / step);
  }
  renderer.fillRect(kGutter, 480, width - 2 * kGutter, 1, true);
  if (v.phase == cp::Phase::Playing || v.phase == cp::Phase::Judging) {
    std::snprintf(status, sizeof(status), tr(STR_CAW_FORMAT_FORMAT_ANSWERS_RECEIVED), v.submitted, v.expected);
    renderer.drawText(UI_10_FONT_ID, kGutter, 498, status, true);
  } else if (v.phase == cp::Phase::Reveal || v.phase == cp::Phase::Finished) {
    const char* winner = v.winner == cp::kRando    ? tr(STR_CAW_RANDO_CARDRISSIAN)
                         : v.winner < cp::kPlayers ? v.players[v.winner].name
                                                   : tr(STR_CAW_NO_WINNER);
    renderer.drawText(SMALL_FONT_ID, kGutter, 494, tr(STR_CAW_ROUND_WINNER), true);
    renderer.drawText(UI_10_FONT_ID, kGutter, 520, winner, true);
    if (v.selected < v.plays && v.prompt < deck.promptsCount) {
      // Same complete Pick 1/2/3 sentence as the phones; preserving the
      // original, unmodified card language and placeholder order.
      const char* cards[cp::kPickMax] = {};
      for (uint8_t i = 0; i < v.pick; ++i) {
        const uint16_t id = v.pile[v.selected].cards[i];
        cards[i] = id < deck.answersCount ? deck.answers[id].text : "";
      }
      char sentence[768] = {};
      cp::sentence::compose(deck.prompts[v.prompt].text, cards, v.pick, sentence, sizeof(sentence));
      drawRichSentence(deck.prompts[v.prompt].text, cards, v.pick, kGutter, 556, textWidth);
    }
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, width - 2 * kGutter, 2, true);
  char action[48] = {};
  std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_WAITING));
  if (v.phase == cp::Phase::Playing) std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_CLOSE_ANSWERS));
  if (v.phase == cp::Phase::Judging) std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_ABORT_ROUND));
  if (v.phase == cp::Phase::Reveal) std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_NEXT_ROUND));
  if (blockedOn != cp::kNone) std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_GO_ON_WITHOUT));
  if (pausedBy != cp::kNone) std::snprintf(action, sizeof(action), "%s", tr(STR_CAW_CONTINUE));
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, action, true);
  renderer.drawText(UI_10_FONT_ID, width - kGutter - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_CAW_MENU)), bottom + 8,
                    tr(STR_CAW_MENU), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
  drawDiagnostics();
}
void CardPartyActivity::drawFinal() {
  const cp::PublicView v = game_.publicView();
  const cp::Table& table = server_.table();
  drawBase(table.ended() ? tr(STR_CAW_ENDED_BY_THE_HOST) : tr(STR_CAW_GAME_OVER));
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  uint16_t best = 0;
  uint8_t seated = 0;
  for (const auto& p : v.players)
    if (p.seated) {
      ++seated;
      if (p.points > best) best = p.points;
    }
  uint8_t order[cp::kPlayers] = {};
  bool listed[cp::kPlayers] = {};
  for (uint8_t rank = 0; rank < seated; ++rank) {
    uint8_t next = cp::kNone;
    for (uint8_t i = 0; i < cp::kPlayers; ++i)
      if (v.players[i].seated && !listed[i] && (next == cp::kNone || v.players[i].points > v.players[next].points))
        next = i;
    listed[next] = true;
    order[rank] = next;
  }
  const auto crown = [&](int cx, int top, int half, bool black) {
    const int xs[] = {cx - half, cx - half, cx - half / 2, cx, cx + half / 2, cx + half, cx + half};
    const int ys[] = {top + half * 6 / 5,   top + half * 4 / 15, top + half * 11 / 15, top,
                      top + half * 11 / 15, top + half * 4 / 15, top + half * 6 / 5};
    renderer.fillPolygon(xs, ys, 7, black);
  };
  char line[64] = {};
  int y = 92;
  // Inverted winner block, readable across the room: a crown over every
  // leader (ties share it), then the standings as bars.
  uint8_t leaders = 0;
  for (uint8_t r = 0; r < seated; ++r)
    if (v.players[order[r]].points == best) ++leaders;
  const int shown = std::min<int>(leaders, 3);
  const int block = 140 + 34 * (shown - 1);
  renderer.fillRect(kGutter, y, textWidth, block, true);
  crown(width / 2, y + 14, 26, false);
  for (int r = 0; r < shown; ++r) {
    const auto& p = v.players[order[r]];
    const int nameWidth = std::min(renderer.getTextWidth(UI_12_FONT_ID, p.name), textWidth - 20);
    renderer.drawText(UI_12_FONT_ID, width / 2 - nameWidth / 2, y + 58 + r * 34, p.name, false);
  }
  std::snprintf(line, sizeof(line), tr(STR_CAW_FORMAT_POINTS), static_cast<unsigned>(best));
  renderer.drawText(SMALL_FONT_ID, width / 2 - renderer.getTextWidth(SMALL_FONT_ID, line) / 2, y + block - 30, line,
                    false);
  y += block + 22;
  const int barMax = textWidth - 150;
  for (uint8_t r = 0; r < seated; ++r) {
    const auto& p = v.players[order[r]];
    renderer.drawText(SMALL_FONT_ID, kGutter, y + 4, p.name, true);
    const int bar = best ? barMax * p.points / best : 0;
    renderer.fillRect(kGutter + 110, y + 4, std::max(bar, 2), 18, true);
    std::snprintf(line, sizeof(line), "%u", static_cast<unsigned>(p.points));
    renderer.drawText(SMALL_FONT_ID, width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, line), y + 4, line, true);
    y += 32;
  }
  const int bottom = renderer.getScreenHeight() - 72;
  std::snprintf(line, sizeof(line), tr(STR_CAW_FORMAT_OF_FORMAT_WANT_ANOTHER_GAME), static_cast<unsigned>(table.votes()),
                static_cast<unsigned>(seated));
  renderer.drawText(SMALL_FONT_ID, kGutter, bottom - 30, line, true);
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_AGAIN), true);
  renderer.drawText(UI_10_FONT_ID, width - kGutter - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_CAW_END)), bottom + 8,
                    tr(STR_CAW_END), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawRules() {
  drawBase(tr(STR_CAW_RULES));
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  const bool fixedRounds = hosting() && game_.phase() != cp::Phase::Lobby;
  renderer.drawText(SMALL_FONT_ID, kGutter, 82,
                    fixedRounds ? tr(STR_CAW_TAP_TO_CHANGE_ROUNDS_FIXED_WHILE_PLAYING) : tr(STR_CAW_TAP_TO_CHANGE), true);
  const auto secs = [](uint8_t value, char* out, size_t size) {
    if (value)
      std::snprintf(out, size, tr(STR_CAW_FORMAT_S), static_cast<unsigned>(value));
    else
      std::snprintf(out, size, "%s", tr(STR_CAW_OFF));
  };
  char values[6][12] = {};
  std::snprintf(values[0], sizeof(values[0]), "%u", static_cast<unsigned>(settings_.rounds));
  secs(settings_.answerSecs, values[1], sizeof(values[1]));
  secs(settings_.extensionSecs, values[2], sizeof(values[2]));
  secs(settings_.jurorSecs, values[3], sizeof(values[3]));
  secs(settings_.autoSecs, values[4], sizeof(values[4]));
  std::snprintf(values[5], sizeof(values[5]), "%s", settings_.rando ? tr(STR_CAW_ON) : tr(STR_CAW_OFF));
  const char* const kLabels[6] = {tr(STR_CAW_ROUNDS),      tr(STR_CAW_ANSWER_TIMER),    tr(STR_CAW_EXTENSION),
                                             tr(STR_CAW_JUROR_TIMER), tr(STR_CAW_AUTO_NEXT_ROUND), tr(STR_CAW_RANDO_CARDRISSIAN)};
  const char* const kDetails[6] = {tr(STR_CAW_PER_GAME),
                                              tr(STR_CAW_PER_ROUND_FROM_THE_NEXT_ROUND),
                                              tr(STR_CAW_ONCE_WHEN_ANSWERS_ARE_MISSING),
                                              tr(STR_CAW_RUNS_OUT_ROUND_ENDS_WITHOUT_POINTS),
                                              tr(STR_CAW_AFTER_THE_REVEAL),
                                              tr(STR_CAW_A_RANDOM_ANSWER_JOINS_EVERY_ROUND)};
  for (int row = 0; row < 6; ++row) {
    const int y = kListTop + row * kListRow;
    renderer.drawText(UI_10_FONT_ID, kGutter, y + 4, kLabels[row], true);
    renderer.drawText(UI_10_FONT_ID, width - kGutter - renderer.getTextWidth(UI_10_FONT_ID, values[row]), y + 4,
                      values[row], true);
    renderer.drawText(SMALL_FONT_ID, kGutter, y + 30, kDetails[row], true);
    renderer.fillRect(kGutter, y + kListRow - 2, textWidth, 1, true);
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_DONE), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawMenu() {
  drawBase(tr(STR_CAW_HOST_MENU));
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  const cp::Table& table = server_.table();
  const cp::PublicView v = game_.publicView();
  char details[8][64] = {};
  const char* titles[8] = {tr(STR_CAW_PAUSE), tr(STR_CAW_TIME_2), tr(STR_CAW_RULES), tr(STR_CAW_CATEGORIES), tr(STR_CAW_DECK), tr(STR_CAW_PLAYERS_2), tr(STR_CAW_END_GAME), tr(STR_CAW_FAME_TITLE)};
  if (table.pausedBy() != cp::kNone) {
    titles[0] = tr(STR_CAW_CONTINUE_2);
    std::snprintf(details[0], sizeof(details[0]), tr(STR_CAW_PAUSED_BY_FORMAT),
                  table.pausedBy() == cp::kHost ? tr(STR_CAW_THE_HOST) : v.players[table.pausedBy()].name);
  } else {
    std::snprintf(details[0], sizeof(details[0]), "%s", tr(STR_CAW_STOPS_CLOCKS_AND_MOVES_FOR_EVERYONE));
  }
  const bool clock = table.clock() == cp::Table::Clock::Answers || table.clock() == cp::Table::Clock::Juror;
  std::snprintf(details[1], sizeof(details[1]), "%s", clock ? tr(STR_CAW_MORE_TIME_ON_THE_RUNNING_CLOCK) : tr(STR_CAW_NO_CLOCK_RUNNING));
  std::snprintf(details[2], sizeof(details[2]), tr(STR_CAW_FORMAT_ROUNDS_TIMER_FORMAT_AUTO_FORMAT), settings_.rounds,
                settings_.answerSecs ? tr(STR_CAW_ON) : tr(STR_CAW_OFF), settings_.autoSecs ? tr(STR_CAW_ON) : tr(STR_CAW_OFF));
  if (packLoaded_ && sdPack_.categoryCount() > 1) {
    int on = 0;
    for (uint8_t i = 0; i < sdPack_.categoryCount(); ++i)
      if (enabled_ & (1u << i)) ++on;
    std::snprintf(details[3], sizeof(details[3]), tr(STR_CAW_FORMAT_OF_FORMAT_ON_FROM_THE_NEXT_CARD), on,
                  static_cast<unsigned>(sdPack_.categoryCount()));
  } else {
    std::snprintf(details[3], sizeof(details[3]), "%s", tr(STR_CAW_THIS_DECK_HAS_NONE));
  }
  std::snprintf(details[4], sizeof(details[4]), tr(STR_CAW_FORMAT_BETWEEN_ROUNDS),
                packLoaded_ ? sdPack_.title() : tr(STR_CAW_BUILT_IN_DEMO_2));
  uint8_t seated = 0, offline = 0;
  for (const auto& p : v.players)
    if (p.seated) {
      ++seated;
      if (!p.connected) ++offline;
    }
  std::snprintf(details[5], sizeof(details[5]), tr(STR_CAW_FORMAT_PLAYING_FORMAT_OFFLINEFORMAT), static_cast<unsigned>(seated),
                static_cast<unsigned>(offline), table.blockedOn() != cp::kNone ? tr(STR_CAW_WAITING_3) : "");
  std::snprintf(details[6], sizeof(details[6]), "%s", tr(STR_CAW_FINAL_STANDINGS_FOR_EVERYONE_TAP_TWICE));
  std::snprintf(details[7], sizeof(details[7]), "%s", tr(STR_CAW_FAME_HINT));
  for (int row = 0; row < 8; ++row) {
    const int y = kListTop + row * kListRow;
    renderer.drawText(UI_10_FONT_ID, kGutter, y + 4, titles[row], true);
    drawWrapped(SMALL_FONT_ID, details[row], kGutter, y + 30, textWidth, 22, 1);
    renderer.fillRect(kGutter, y + kListRow - 2, textWidth, 1, true);
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_BACK_TO_GAME), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawFame() {
  drawBase(tr(STR_CAW_FAME_TITLE));
  const int width = renderer.getScreenWidth();
  char line[160] = {};
  const size_t playerPages = std::max<size_t>(1, (history_.players() + 7) / 8);
  const size_t page = famePage_;
  const int textWidth = width - 2 * kGutter;
  if (page < playerPages) {
    renderer.drawText(SMALL_FONT_ID, kGutter, 82, tr(STR_CAW_FAME_PLAYERS), true);
    // Sort by wins, then round wins; never modify the historical records.
    uint8_t order[cp::history::kPlayersStored] = {};
    for (size_t i = 0; i < history_.players(); ++i) order[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < history_.players(); ++i)
      for (size_t j = i + 1; j < history_.players(); ++j) {
        const auto& a = history_.at(order[i]);
        const auto& b = history_.at(order[j]);
        if (b.wins > a.wins || (b.wins == a.wins && b.rounds > a.rounds))
          std::swap(order[i], order[j]);
      }
    const size_t start = page * 8, end = std::min(start + 8,history_.players());
    for (size_t i = start; i < end; ++i) {
      const auto& p = history_.at(order[i]);
      const int y = 115 + static_cast<int>(i-start) * 61;
      std::snprintf(line,sizeof(line),"%s",p.name);
      drawWrapped(UI_10_FONT_ID,line,kGutter,y,textWidth,27,1);
      std::snprintf(line,sizeof(line),tr(STR_CAW_FAME_SCORE_FORMAT),
                    static_cast<unsigned>(p.games),static_cast<unsigned>(p.wins),
                    static_cast<unsigned>(p.rounds));
      drawWrapped(SMALL_FONT_ID,line,kGutter,y+27,textWidth,22,1);
      renderer.fillRect(kGutter,y+54,textWidth,1,true);
    }
    if (!history_.players())
      drawWrapped(UI_10_FONT_ID,tr(STR_CAW_FAME_EMPTY),kGutter,120,textWidth,28,2);
  } else {
    const size_t quotePage=page-playerPages;
    renderer.drawText(SMALL_FONT_ID,kGutter,82,tr(STR_CAW_FAME_SENTENCES),true);
    const size_t first=quotePage*4;
    const size_t last=std::min(first+4,history_.quotes());
    for(size_t i=first;i<last;++i) {
      const auto& q=history_.quote(i);
      const auto* p=history_.player(q.player);
      const int y=118+static_cast<int>(i-first)*125;
      drawWrapped(SMALL_FONT_ID,p?p->name:"?",kGutter,y,textWidth,22,1);
      drawWrapped(UI_10_FONT_ID,q.sentence,kGutter,y+25,textWidth,26,3);
      renderer.fillRect(kGutter,y+114,textWidth,1,true);
    }
    if(!history_.quotes())
      drawWrapped(UI_10_FONT_ID,tr(STR_CAW_FAME_EMPTY),kGutter,120,textWidth,28,2);
  }
  const int bottom=renderer.getScreenHeight()-72;
  renderer.fillRect(kGutter,bottom-4,textWidth,2,true);
  renderer.drawText(SMALL_FONT_ID,kGutter,bottom+8,tr(STR_CAW_FAME_PREV),true);
  renderer.drawText(SMALL_FONT_ID,width/3,bottom+8,tr(STR_CAW_FAME_NEXT),true);
  renderer.drawText(SMALL_FONT_ID,width*2/3,bottom+8,tr(STR_CAW_FAME_CLOSE),true);
}
void CardPartyActivity::drawPlayers() {
  drawBase(tr(STR_CAW_PLAYERS_2));
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  const cp::Table& table = server_.table();
  const cp::PublicView v = game_.publicView();
  renderer.drawText(SMALL_FONT_ID, kGutter, 82, tr(STR_CAW_TAP_TWICE_TO_REMOVE_A_PLAYER), true);
  int row = 0;
  char line[96] = {};
  if (table.blockedOn() != cp::kNone) {
    const int y = kListTop;
    std::snprintf(line, sizeof(line), tr(STR_CAW_GO_ON_WITHOUT_FORMAT), v.players[table.blockedOn()].name);
    drawWrapped(UI_10_FONT_ID, line, kGutter, y + 4, textWidth, 26, 1);
    renderer.drawText(SMALL_FONT_ID, kGutter, y + 30, tr(STR_CAW_NO_CONNECTION_FOR_15_S), true);
    renderer.fillRect(kGutter, y + kListRow - 2, textWidth, 2, true);
    ++row;
  }
  for (uint8_t i = 0; i < cp::kPlayers; ++i) {
    const auto& p = v.players[i];
    if (!p.seated) continue;
    const int y = kListTop + row * kListRow;
    renderer.drawText(UI_10_FONT_ID, kGutter, y + 4, p.name, true);
    if (kickArmed_ == i)
      std::snprintf(line, sizeof(line), "%s", tr(STR_CAW_TAP_AGAIN_TO_REMOVE));
    else
      std::snprintf(line, sizeof(line), tr(STR_CAW_FORMAT_PT_FORMATFORMATFORMATFORMAT), static_cast<unsigned>(p.points),
                    p.connected ? tr(STR_CAW_ONLINE) : tr(STR_CAW_OFFLINE), i == v.czar ? tr(STR_CAW_JUROR_2) : "",
                    table.pausedBy() == i ? tr(STR_CAW_PAUSED_2) : "", table.voted(i) ? tr(STR_CAW_WANTS_AGAIN) : "");
    renderer.drawText(SMALL_FONT_ID, kGutter, y + 30, line, true);
    renderer.fillRect(kGutter, y + kListRow - 2, textWidth, 1, true);
    ++row;
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, tr(STR_CAW_DONE), true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
bool CardPartyActivity::matchInProgress() const {
  if (game_.phase() != cp::Phase::Lobby) return true;
  for (const auto& p : game_.publicView().players)
    if (p.seated) return true;
  return false;
}
void CardPartyActivity::drawDiagnostics() {
  // Field telemetry for hardware acceptance; refreshed with each repaint,
  // never on its own (no extra e-ink refreshes). Counts only, no secrets.
  const cp::web::Server::Diagnostics d = server_.diagnostics();
  if (!d.valid) return;
  char line[64] = {};
  std::snprintf(line, sizeof(line), "SYS H%uK M%uK WS%02u AP%02u", static_cast<unsigned>(d.freeHeap / 1024),
                static_cast<unsigned>(d.minFreeHeap / 1024), d.sockets, d.stations);
  const int width = renderer.getScreenWidth();
  const int x = width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, line);
  renderer.drawText(SMALL_FONT_ID, x, renderer.getScreenHeight() - 104, line, true);
}
void CardPartyActivity::drawError() {
  drawBase("ERROR");
  renderer.drawText(UI_10_FONT_ID, kGutter, 138, "COULD NOT START SESSION", true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 198, report_, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, renderer.getScreenHeight() - 76, "[ BACK TO SETUP ]", true);
}
void CardPartyActivity::render(RenderLock&&) {
  switch (stage_) {
    case Stage::Choice:
      drawChoice();
      break;
    case Stage::Decks:
      drawDecks();
      break;
    case Stage::Remote:
      drawRemote();
      break;
    case Stage::Categories:
      drawCategories();
      break;
    case Stage::Rules:
      drawRules();
      break;
    case Stage::Menu:
      drawMenu();
      break;
    case Stage::Players:
      drawPlayers();
      break;
    case Stage::Fame:
      drawFame();
      break;
    case Stage::Hosted:
      if (game_.phase() == cp::Phase::Lobby)
        drawLobby();
      else
        drawGame();
      break;
    case Stage::Failed:
      drawError();
      break;
  }
  renderer.displayBuffer();
}
