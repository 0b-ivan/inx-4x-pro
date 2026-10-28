#include "CardPartyActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "CardPartyTextWrap.h"
#ifdef SIMULATOR
#include <cstdlib>
#endif
#include "../../DevMode.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
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
// The host's category switches, one record per pack, keyed to its CRC.
constexpr const char* kCardPartySelection = "/games/cardparty/categories.bin";
constexpr uint8_t kHand = 5;
// Category list on the X4: one row per category, below the hint line.
constexpr int kCategoryTop = 112;
constexpr int kCategoryRow = 36;
// Deck list: the built-in demo, then one row per pack file.
constexpr int kDeckTop = 112;
constexpr int kDeckRow = 56;
// Setup boxes, top and bottom: [01] hotspot, [02] Wi-Fi, [03] deck, [04] categories.
constexpr int kBoxTop[] = {145, 240, 335, 430};
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
      return "WAITING FOR PLAYERS";
    case cp::Phase::Playing:
      return "COLLECTING ANSWERS";
    case cp::Phase::Judging:
      return "CARD CZAR DECIDES";
    case cp::Phase::Reveal:
      return "ROUND FINISHED";
    case cp::Phase::Finished:
      return "GAME OVER";
  }
  return "WAITING";
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
  {
    // The Game holds pointers into the pack until resetGame() reconfigures it.
    RenderLock lock(*this);
    if (row == 0) {
      sdPack_.clear();
      packLoaded_ = false;
      deckPath_[0] = 0;
      activeDeck_ = cp::demoDeck();
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
void CardPartyActivity::applyCategories() {
  if (!packLoaded_) {
    deckPlayable_ = true;
    return;
  }
  if (sdPack_.categoryCount() == 0) {
    activeDeck_ = sdPack_.deck();
  } else if (filtered_.build(sdPack_, enabled_)) {
    activeDeck_ = filtered_.deck();
  } else {
    LOG_ERR("CARDPARTY", "OOM: category filter, using original demo");
    activeDeck_ = cp::demoDeck();
  }
  deckPlayable_ = cp::categories::playable(activeDeck_, kHand);
}
void CardPartyActivity::leaveCategories() {
  saveSelection();
  resetGame();
  requestUpdate();
}
void CardPartyActivity::resetGame() {
  abortArmed_ = false;
  abortArmedAt_ = 0;
  backArmed_ = false;
  backArmedAt_ = 0;
  const auto rules = cp::Rules{kHand, 10, true};
  if (!deckPlayable_) {
    // Switched off too much: stay in setup and say why instead of failing.
    stage_ = Stage::Choice;
    std::snprintf(report_, sizeof(report_), "TOO FEW CARDS: SWITCH ON MORE CATEGORIES");
  } else if (!game_.configure(activeDeck_, rules, toybox::seed())) {
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_), "DECK INVALID");
  } else {
    stage_ = Stage::Choice;
    report_[0] = 0;
  }
  sessions_.reset();
  fingerprint_ = visibleFingerprint(game_.publicView());
}
void CardPartyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadConfiguredDeck();
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
  } else if (scenario && *scenario && std::strcmp(scenario, "setup") != 0)
    start(cp::web::Server::Mode::Hotspot);
#endif
  requestUpdate();
}
void CardPartyActivity::onExit() {
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
  if (!server_.begin(mode)) {
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_),
                  mode == cp::web::Server::Mode::Hotspot ? "HOTSPOT COULD NOT START" : "CONNECT TO WI-FI FIRST");
  } else {
    stage_ = Stage::Hosted;
    fingerprint_ = visibleFingerprint(game_.publicView());
  }
  requestUpdate();
}
void CardPartyActivity::chooseExistingWifi() {
  if (!deckPlayable_) {
    requestUpdate();
    return;
  }
  // Already associated (dev mode keeps its last network): host right away.
  if (WiFi.status() == WL_CONNECTED) {
    start(cp::web::Server::Mode::ExistingWifi);
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
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      releaseNetwork();
      stage_ = Stage::Choice;
      requestUpdate();
      return;
    }
    const auto& wifi = std::get<WifiResult>(result.data);
    if (!wifi.connected) {
      releaseNetwork();
      stage_ = Stage::Failed;
      std::snprintf(report_, sizeof(report_), "WI-FI NOT CONNECTED");
      requestUpdate();
      return;
    }
    start(cp::web::Server::Mode::ExistingWifi);
  });
}
void CardPartyActivity::handleTap(int x, int y) {
  const int height = renderer.getScreenHeight();
  const int width = renderer.getScreenWidth();
  if (stage_ == Stage::Choice) {
    const auto inBox = [y](int i) { return y >= kBoxTop[i] && y < kBoxTop[i] + kBoxHeight; };
    if (inBox(0)) {
      start(cp::web::Server::Mode::Hotspot);
    } else if (inBox(1)) {
      chooseExistingWifi();
    } else if (inBox(2)) {
      scanDecks();
      stage_ = Stage::Decks;
      report_[0] = 0;
      requestUpdate();
    } else if (inBox(3) && packLoaded_ && sdPack_.categoryCount() > 1) {
      stage_ = Stage::Categories;
      report_[0] = 0;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Decks) {
    const int row = (y - kDeckTop) / kDeckRow;
    if (y >= kDeckTop && row <= deckCount_) {
      chooseDeck(row);
    } else if (y >= height - 90) {
      stage_ = Stage::Choice;
      report_[0] = 0;
      requestUpdate();
    }
    return;
  }
  if (stage_ == Stage::Categories) {
    const int row = (y - kCategoryTop) / kCategoryRow;
    if (y >= kCategoryTop && row < sdPack_.categoryCount()) {
      enabled_ ^= static_cast<uint16_t>(1u << row);
      applyCategories();
      requestUpdate();
    } else if (y >= height - 90) {
      leaveCategories();
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
  if (stage_ != Stage::Hosted || x < 0 || x > width || y < height - 90) return;
  const cp::Phase phase = game_.phase();
  if (phase == cp::Phase::Finished) {
    // Same hotspot, password and seats: phones land in the new lobby.
    abortArmed_ = backArmed_ = false;
    if (server_.hostRematch())
      report_[0] = 0;
    else
      std::snprintf(report_, sizeof(report_), "ACTION NOT AVAILABLE");
    requestUpdate();
    return;
  }
  bool accepted = false;
  backArmed_ = false;
  if (phase == cp::Phase::Judging) {
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
  } else {
    abortArmed_ = false;
    if (phase == cp::Phase::Lobby)
      accepted = server_.hostStart();
    else if (phase == cp::Phase::Playing)
      accepted = server_.hostClose();
    else if (phase == cp::Phase::Reveal)
      accepted = server_.hostNext();
  }
  if (!accepted) {
    std::snprintf(report_, sizeof(report_),
                  phase == cp::Phase::Lobby     ? "NEED 3 CONNECTED PLAYERS"
                  : phase == cp::Phase::Playing ? "NO ANSWERS RECEIVED YET"
                                                : "ACTION NOT AVAILABLE");
  } else
    report_[0] = 0;
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
    if (stage_ == Stage::Decks) {
      stage_ = Stage::Choice;
      report_[0] = 0;
      requestUpdate();
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
    const uint32_t next = visibleFingerprint(game_.publicView());
    if (next != fingerprint_) {
      fingerprint_ = next;
      requestUpdate();
    }
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
  drawBase("SETUP");
  renderer.drawText(UI_10_FONT_ID, kGutter, 92, "CARDS AGAINST WHATEVER", true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 118, "X4 = GAME MASTER  /  PHONES = CARDS", true);
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  const auto box = [&](int i, const char* title, const char* detail) {
    renderer.drawRect(kGutter, kBoxTop[i], textWidth, kBoxHeight, 2, true);
    renderer.drawText(UI_10_FONT_ID, kGutter + 20, kBoxTop[i] + 21, title, true);
    drawWrapped(SMALL_FONT_ID, detail, kGutter + 20, kBoxTop[i] + 53, textWidth - 40, 22, 1);
  };
  box(0, "[01] CREATE HOTSPOT", "LOCAL / PASSWORD PROTECTED");
  box(1, "[02] EXISTING WI-FI", "PICK A NETWORK / SAME AS PLAYERS");
  box(2, "[03] DECK", packLoaded_ ? sdPack_.title() : "BUILT-IN DEMO / TAP TO CHOOSE A PACK");
  // A single category has nothing to switch: off would only empty the deck.
  if (packLoaded_ && sdPack_.categoryCount() > 1) {
    int on = 0;
    for (uint8_t i = 0; i < sdPack_.categoryCount(); ++i)
      if (enabled_ & (1u << i)) ++on;
    char summary[64] = {};
    std::snprintf(summary, sizeof(summary), "%d OF %u ON / %u PROMPTS / %u ANSWERS", on,
                  static_cast<unsigned>(sdPack_.categoryCount()), static_cast<unsigned>(activeDeck_.promptsCount),
                  static_cast<unsigned>(activeDeck_.answersCount));
    box(3, "[04] CATEGORIES", summary);
  }
  if (report_[0]) drawWrapped(SMALL_FONT_ID, report_, kGutter, 535, textWidth, 24, 2);
  if (packLoaded_) {
    // Attribution for the loaded pack, as its license requires.
    char line[420] = {};
    std::snprintf(line, sizeof(line), "%s / %s", sdPack_.rightsHolder(), sdPack_.license());
    drawWrapped(SMALL_FONT_ID, line, kGutter, renderer.getScreenHeight() - 138, textWidth, 22, 3);
  }
  renderer.drawText(SMALL_FONT_ID, kGutter, renderer.getScreenHeight() - 35, "BACK  /  CANCEL", true);
}
void CardPartyActivity::drawDecks() {
  drawBase("DECK");
  const int width = renderer.getScreenWidth();
  const int textWidth = width - 2 * kGutter;
  renderer.drawText(SMALL_FONT_ID, kGutter, 82, "TAP TO LOAD  /  SD: GAMES/CARDPARTY/PACKS", true);
  for (int row = 0; row <= deckCount_; ++row) {
    const int y = kDeckTop + row * kDeckRow;
    const DeckEntry* entry = row ? &decks_[row - 1] : nullptr;
    const bool current = entry ? packLoaded_ && std::strcmp(entry->path, deckPath_) == 0 : !packLoaded_;
    const char* title = !entry                                    ? "Built-in demo"
                        : entry->readable && entry->info.title[0] ? entry->info.title
                                                                  : entry->file;
    char line[96] = {};
    std::snprintf(line, sizeof(line), "%s %s", current ? "[x]" : "[  ]", title);
    drawWrapped(UI_10_FONT_ID, line, kGutter, y + 4, textWidth, 26, 1);
    if (!entry)
      std::snprintf(line, sizeof(line), "ENGLISH / ALWAYS AVAILABLE");
    else if (!entry->readable)
      std::snprintf(line, sizeof(line), "%s / NOT A CAW PACK", entry->file);
    else if (!entry->info.released)
      std::snprintf(line, sizeof(line), "%s / DRAFT, NOT PLAYABLE", entry->file);
    else
      std::snprintf(line, sizeof(line), "%s / %u PROMPTS / %u ANSWERS", entry->file,
                    static_cast<unsigned>(entry->info.prompts), static_cast<unsigned>(entry->info.answers));
    drawWrapped(SMALL_FONT_ID, line, kGutter + 34, y + 30, textWidth - 34, 22, 1);
    renderer.fillRect(kGutter, y + kDeckRow - 2, textWidth, 1, true);
  }
  if (deckCount_ == 0)
    drawWrapped(SMALL_FONT_ID, "NO PACKS ON THE CARD. COPY .DAT FILES TO /games/cardparty/packs/", kGutter,
                kDeckTop + kDeckRow + 16, textWidth, 24, 2);
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, textWidth, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, "[ DONE ]", true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
}
void CardPartyActivity::drawCategories() {
  drawBase("CATEGORIES");
  const int width = renderer.getScreenWidth();
  renderer.drawText(SMALL_FONT_ID, kGutter, 82, "TAP TO SWITCH  /  18+ STARTS OFF", true);
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
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, "[ DONE ]", true);
  char status[80] = {};
  if (deckPlayable_)
    std::snprintf(status, sizeof(status), "%u PROMPTS / %u ANSWERS IN PLAY",
                  static_cast<unsigned>(activeDeck_.promptsCount), static_cast<unsigned>(activeDeck_.answersCount));
  else
    std::snprintf(status, sizeof(status), "TOO FEW CARDS: %u OF %u ANSWERS",
                  static_cast<unsigned>(activeDeck_.answersCount),
                  static_cast<unsigned>(cp::categories::requiredAnswers(activeDeck_, kHand)));
  renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, status, true);
}
void CardPartyActivity::drawLobby() {
  drawBase("JOIN SESSION");
  const int width = renderer.getScreenWidth();
  renderer.drawText(UI_10_FONT_ID, kGutter, 90, "WAITING FOR PLAYERS", true);
  int connected = 0;
  const cp::PublicView view = game_.publicView();
  for (const auto& p : view.players)
    if (p.seated && p.connected) ++connected;
  char stats[64] = {};
  std::snprintf(stats, sizeof(stats), "CONNECTED: %d / 8    REQUIRED: 3", connected);
  renderer.drawText(SMALL_FONT_ID, kGutter, 128, stats, true);

  // 2 QR codes in a WPA2 hotspot: join the private Wi-Fi first, then open
  // the actual game URL in a regular browser (not captive-portal mini UI).
  if (server_.hotspot()) {
    char wifiQr[128] = {};
    std::snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", server_.ssid(), server_.password());
    QrUtils::drawQrCode(renderer, Rect{kGutter, 157, kQr, kQr}, wifiQr);
    renderer.drawText(SMALL_FONT_ID, kGutter, 323, "01 / JOIN WI-FI", true);
    renderer.drawText(SMALL_FONT_ID, kGutter, 347, server_.password(), true);
    QrUtils::drawQrCode(renderer, Rect{kGutter + kQr + 32, 157, kQr, kQr}, server_.url());
    renderer.drawText(SMALL_FONT_ID, kGutter + kQr + 32, 323, "02 / OPEN GAME", true);
  } else {
    QrUtils::drawQrCode(renderer, Rect{kGutter, 157, kQr, kQr}, server_.url());
    renderer.drawText(SMALL_FONT_ID, kGutter, 323, "OPEN GAME", true);
  }
  // Network, URL and roster as plain text too, so joining works without a camera.
  renderer.drawText(SMALL_FONT_ID, kGutter, 385, server_.ssid(), true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 409, server_.url(), true);
  renderer.fillRect(kGutter, 440, width - 2 * kGutter, 1, true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 452, "/// PLAYERS", true);
  int y = 482;
  for (const auto& p : view.players) {
    if (!p.seated) continue;
    renderer.drawText(UI_10_FONT_ID, kGutter, y, p.name, true);
    if (!p.connected)
      renderer.drawText(SMALL_FONT_ID, width - kGutter - renderer.getTextWidth(SMALL_FONT_ID, "OFFLINE"), y + 4,
                        "OFFLINE", true);
    y += 26;
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, width - 2 * kGutter, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, connected >= 3 ? "[ START GAME ]" : "[ WAITING FOR 3 PLAYERS ]",
                    true);
  if (report_[0]) renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
  drawDiagnostics();
}
// Greedy word wrap by measured width; returns the y below the last row.
// Returns the y below the last row. With draw=false it only counts rows, so a
// caller can pick the largest font that fits.
int CardPartyActivity::drawWrapped(int fontId, const char* text, int x, int y, int maxWidth, int lineStep, int maxRows,
                                   bool draw) {
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
    if (draw) renderer.drawText(fontId, x, y + row * lineStep, line, true);
    ++row;
  }
  return y + row * lineStep;
}
void CardPartyActivity::drawGame() {
  const cp::PublicView v = game_.publicView();
  drawBase(phaseLabel(v.phase));
  const int width = renderer.getScreenWidth();
  char status[96] = {};
  std::snprintf(status, sizeof(status), "ROUND %02u / 10", v.round);
  renderer.drawText(SMALL_FONT_ID, kGutter, 86, status, true);
  const char* czar = v.czar < cp::kPlayers ? v.players[v.czar].name : "-";
  std::snprintf(status, sizeof(status), "CZAR: %.22s", czar);
  renderer.drawText(SMALL_FONT_ID, kGutter, 110, status, true);
  renderer.fillRect(kGutter, 140, width - 2 * kGutter, 1, true);
  renderer.drawText(SMALL_FONT_ID, kGutter, 156, "/// PROMPT", true);
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
    std::snprintf(status, sizeof(status), "%u / %u ANSWERS RECEIVED", v.submitted, v.expected);
    renderer.drawText(UI_10_FONT_ID, kGutter, 498, status, true);
  } else if (v.phase == cp::Phase::Reveal || v.phase == cp::Phase::Finished) {
    const char* winner = v.winner == cp::kRando    ? "RANDO CARDRISSIAN"
                         : v.winner < cp::kPlayers ? v.players[v.winner].name
                                                   : "NO WINNER";
    renderer.drawText(SMALL_FONT_ID, kGutter, 494, "/// ROUND WINNER", true);
    renderer.drawText(UI_10_FONT_ID, kGutter, 520, winner, true);
    if (v.selected < v.plays) {
      // The winning card(s), in play order, joined like the phones show them.
      char answer[600] = {};
      size_t used = 0;
      for (uint8_t i = 0; i < v.pick; ++i) {
        const uint16_t id = v.pile[v.selected].cards[i];
        const char* text = id < deck.answersCount ? deck.answers[id].text : "";
        used += std::snprintf(answer + used, sizeof(answer) - used, "%s%s", i ? " / " : "", text);
        if (used >= sizeof(answer) - 1) break;
      }
      drawWrapped(UI_10_FONT_ID, answer, kGutter, 556, textWidth, 30, 4);
    }
  }
  const int bottom = renderer.getScreenHeight() - 72;
  renderer.fillRect(kGutter, bottom - 4, width - 2 * kGutter, 2, true);
  const char* action = "[ WAITING ]";
  if (v.phase == cp::Phase::Playing) action = "[ CLOSE SUBMISSIONS ]";
  if (v.phase == cp::Phase::Judging) action = "[ ABORT STALLED ROUND ]";
  if (v.phase == cp::Phase::Reveal) action = "[ NEXT ROUND ]";
  if (v.phase == cp::Phase::Finished) action = "[ REMATCH ]";
  renderer.drawText(UI_10_FONT_ID, kGutter, bottom + 8, action, true);
  if (report_[0])
    renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, report_, true);
  else if (v.phase == cp::Phase::Finished)
    renderer.drawText(SMALL_FONT_ID, kGutter, bottom + 42, "BACK x2 = END SESSION", true);
  drawDiagnostics();
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
    case Stage::Categories:
      drawCategories();
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
