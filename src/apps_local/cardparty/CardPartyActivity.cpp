#include "CardPartyActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
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
// Immutable, released SD pack. Missing/invalid/unapproved packs fall back to
// the self-contained demo; never accept a .tmp file or draft pack as active.
constexpr const char* kCardPartyActivePack = "/games/cardparty/active.dat";
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
  HalFile file;
  if (!Storage.openFileForRead("CARDPARTY", kCardPartyActivePack, file)) {
    return;  // No optional pack is normal.
  }
  if (file.size() > cp::sdpack::kMaxPackBytes) {
    LOG_ERR("CARDPARTY", "SD pack exceeds memory budget, using original demo");
    file.close();
    return;
  }
  CardPartySdSource source(file);
  const bool accepted = sdPack_.load(source);  // draft packs are rejected
  file.close();
  if (accepted) {
    activeDeck_ = sdPack_.deck();
    LOG_INF("CARDPARTY", "Loaded released SD deck v%u (%u prompts, %u answers)",
            static_cast<unsigned>(sdPack_.version()), static_cast<unsigned>(activeDeck_.promptsCount),
            static_cast<unsigned>(activeDeck_.answersCount));
  } else {
    LOG_ERR("CARDPARTY", "Rejected invalid/unapproved SD pack, using original demo");
  }
}
void CardPartyActivity::resetGame() {
  abortArmed_ = false;
  abortArmedAt_ = 0;
  backArmed_ = false;
  backArmedAt_ = 0;
  const auto rules = cp::Rules{5, 10, true};
  if (!game_.configure(activeDeck_, rules, toybox::seed())) {
    stage_ = Stage::Failed;
    std::snprintf(report_, sizeof(report_), "DEMO DECK INVALID");
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
  if (scenario && *scenario && std::strcmp(scenario, "setup") != 0) start(cp::web::Server::Mode::Hotspot);
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
    if (y >= 145 && y < 232)
      start(cp::web::Server::Mode::Hotspot);
    else if (y >= 240 && y < 325)
      chooseExistingWifi();
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
  renderer.drawText(SMALL_FONT_ID, kGutter, 125, "X4 = GAME MASTER  /  PHONES = CARDS", true);
  const int width = renderer.getScreenWidth();
  renderer.drawRect(kGutter, 145, width - 2 * kGutter, 86, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter + 20, 174, "[01] CREATE HOTSPOT", true);
  renderer.drawText(SMALL_FONT_ID, kGutter + 20, 205, "LOCAL / PASSWORD PROTECTED", true);
  renderer.drawRect(kGutter, 240, width - 2 * kGutter, 85, 2, true);
  renderer.drawText(UI_10_FONT_ID, kGutter + 20, 270, "[02] EXISTING WI-FI", true);
  renderer.drawText(SMALL_FONT_ID, kGutter + 20, 304, "PICK A NETWORK / SAME AS PLAYERS", true);
  renderer.drawText(SMALL_FONT_ID, kGutter, renderer.getScreenHeight() - 35, "BACK  /  CANCEL", true);
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
int CardPartyActivity::drawWrapped(int fontId, const char* text, int x, int y, int maxWidth, int lineStep,
                                   int maxRows) {
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
    if (row == maxRows - 1 && *text) cp::markTextTruncated(line, sizeof(line), maxWidth, measure, &context);
    renderer.drawText(fontId, x, y + row * lineStep, line, true);
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
  if (v.prompt < deck.promptsCount)
    drawWrapped(UI_10_FONT_ID, deck.prompts[v.prompt].text, kGutter, 190, width - 2 * kGutter, 38, 9);
  renderer.fillRect(kGutter, 540, width - 2 * kGutter, 1, true);
  if (v.phase == cp::Phase::Playing || v.phase == cp::Phase::Judging) {
    std::snprintf(status, sizeof(status), "%u / %u ANSWERS RECEIVED", v.submitted, v.expected);
    renderer.drawText(UI_10_FONT_ID, kGutter, 558, status, true);
  } else if (v.phase == cp::Phase::Reveal || v.phase == cp::Phase::Finished) {
    const char* winner = v.winner == cp::kRando    ? "RANDO CARDRISSIAN"
                         : v.winner < cp::kPlayers ? v.players[v.winner].name
                                                   : "NO WINNER";
    renderer.drawText(SMALL_FONT_ID, kGutter, 556, "/// ROUND WINNER", true);
    renderer.drawText(UI_10_FONT_ID, kGutter, 584, winner, true);
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
