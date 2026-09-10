#include "BattleshipLocalPlayActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../../../DevMode.h"
#include "../../../activities/network/WifiSelectionActivity.h"
#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../../util/QrUtils.h"
#include "../../player/PlayerAvatar.h"
#include "../../ui/Toybox.h"
#include "../../ui/ToyboxFonts.h"
#include "../../ui/ToyboxSeed.h"
#include "../../ui/ToyboxTheme.h"
#include "../BattleshipScreens.h"

namespace {
namespace fui = freeink::ui;

constexpr fui::ActionId kActionNetworkRow = 7;
constexpr int kQrSize = 170;

void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

void buildNetworkChoice(toybox::Screen& screen, const int selected) {
  toyboxChrome(screen, "BROWSER PLAY");

  const fui::Rect intro = screen.takeTop(34);
  fui::TextStyle style;
  style.font = toybox::kTileFont;
  style.align = fui::TextAlign::Left;
  screen.target().text(intro, "CONNECT THROUGH", style);

  fui::ListItem rows[2] = {};
  rows[0].label = "WI-FI";
  rows[0].actionValue = 0;
  rows[1].label = "HOTSPOT";
  rows[1].actionValue = 1;

  fui::ListProps list;
  list.items = rows;
  list.count = 2;
  list.selectedIndex = static_cast<int16_t>(selected);
  list.action = kActionNetworkRow;
  const int16_t listHeight = static_cast<int16_t>(2 * toybox::kRowHeight + toybox::kGutter / 2 + toybox::kGutter);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);
}

}  // namespace

void BattleshipLocalPlayActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  seed_ = toybox::seed();
  stage_ = Stage::Transport;
  selected_ = 0;
  browser_.setCommands(
      this,
      [](void* context, const bshipweb::Command& command, bshipweb::PlacementView& view) {
        auto& activity = *static_cast<BattleshipLocalPlayActivity*>(context);
        RenderLock lock(activity);
        const bool accepted = activity.browserPlayer_.apply(activity.browserGame_, command);
        view = activity.browserPlayer_.view();
        if (accepted) {
          if (view.ready && activity.stage_ == Stage::BrowserWaiting) activity.startX4Placement();
          activity.requestUpdate();
        }
        return accepted;
      },
      [](void* context) {
        auto& activity = *static_cast<BattleshipLocalPlayActivity*>(context);
        RenderLock lock(activity);
        if (!activity.browserPlayer_.view().ready) activity.browserPlayer_.reset();
        activity.requestUpdate();
      });
  requestUpdate();
}

void BattleshipLocalPlayActivity::onExit() {
  cleanupBrowserNetwork();
  if (devModePaused_) {
    devmode::resume();
    devModePaused_ = false;
  }
  Activity::onExit();
}

void BattleshipLocalPlayActivity::finishCancelled() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}

void BattleshipLocalPlayActivity::cleanupBrowserNetwork() {
  browser_.stop();
  lastClientSeen_ = false;

  if (ownsSta_) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    ownsSta_ = false;
  }
}

void BattleshipLocalPlayActivity::chooseTransport(const int index) {
  selected_ = index;
  if (index == 0) {
    MenuResult result;
    result.action = 0;  // existing X4 Pro / ESP-NOW path
    setResult(ActivityResult{result});
    finish();
    return;
  }

  if (!devModePaused_) {
    devmode::pause();
    devModePaused_ = true;
  }
  stage_ = Stage::Network;
  selected_ = 0;
  requestUpdate();
}

void BattleshipLocalPlayActivity::chooseNetwork(const int index) {
  selected_ = index;
  if (index == 0) {
    startWifiBrowser();
  } else {
    startHotspotBrowser();
  }
}

void BattleshipLocalPlayActivity::startWifiBrowser() {
  cleanupBrowserNetwork();
  WiFi.mode(WIFI_STA);
  ownsSta_ = true;

  auto wifiActivity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiActivity) {
    LOG_ERR("BSHIPWEB", "Could not allocate Wi-Fi selection activity");
    cleanupBrowserNetwork();
    requestUpdate();
    return;
  }

  startActivityForResult(std::move(wifiActivity), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      cleanupBrowserNetwork();
      stage_ = Stage::Network;
      selected_ = 0;
      requestUpdate();
      return;
    }

    const auto& wifi = std::get<WifiResult>(result.data);
    if (!wifi.connected || !browser_.begin(bshipweb::Server::NetworkMode::ExistingWifi)) {
      LOG_ERR("BSHIPWEB", "Could not start browser server on selected Wi-Fi");
      cleanupBrowserNetwork();
      stage_ = Stage::Network;
      requestUpdate();
      return;
    }
    enterBrowserWaiting();
  });
}

void BattleshipLocalPlayActivity::startHotspotBrowser() {
  cleanupBrowserNetwork();
  ownsSta_ = false;
  if (!browser_.begin(bshipweb::Server::NetworkMode::Hotspot)) {
    LOG_ERR("BSHIPWEB", "Could not start Battleship hotspot");
    stage_ = Stage::Network;
    requestUpdate();
    return;
  }
  enterBrowserWaiting();
}

void BattleshipLocalPlayActivity::enterBrowserWaiting() {
  bship::reset(browserGame_);
  // Browser owns side 1 and places first; the X4 fleet stays entirely on the host.
  browserGame_.turn = 1;
  browserPlayer_.reset();
  selectedX4Ship_ = -1;
  x4Status_[0] = '\0';
  browser_.publish(bshipweb::browserSnapshot(browserGame_, false));
  stage_ = Stage::BrowserWaiting;
  selected_ = 0;
  lastClientSeen_ = browser_.clientSeen();
  requestUpdate();
}

void BattleshipLocalPlayActivity::startX4Placement() {
  bship::randomFleet(x4Fleet_, seed_);
  selectedX4Ship_ = -1;
  std::snprintf(x4Status_, sizeof(x4Status_), "BROWSER READY - SET YOUR FLEET");
  stage_ = Stage::X4Placement;
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
}

void BattleshipLocalPlayActivity::commitX4Fleet() {
  if (stage_ != Stage::X4Placement) return;
  if (!bship::place(browserGame_, 0, x4Fleet_)) {
    LOG_ERR("BSHIPWEB", "X4 fleet was refused after browser placement");
    std::snprintf(x4Status_, sizeof(x4Status_), "FLEET COULD NOT BE SET");
    requestUpdate();
    return;
  }

  selectedX4Ship_ = -1;
  std::snprintf(x4Status_, sizeof(x4Status_), "BOTH FLEETS ARE SET");
  stage_ = Stage::Playing;
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
  requestUpdate();
}

BattleshipLocalPlayActivity::GridGeometry BattleshipLocalPlayActivity::x4PlaceGrid() const {
  GridGeometry grid;
  const int byWidth = (x4BodySlot_.width - 2 * toybox::kBoardFrame) / bship::kSize;
  const int byHeight = (x4BodySlot_.height - 2 * toybox::kBoardFrame) / bship::kSize;
  grid.cell = std::clamp(std::min(byWidth, byHeight), 8, 44);
  grid.originX = x4BodySlot_.x + (x4BodySlot_.width - grid.cell * bship::kSize) / 2;
  grid.originY = x4BodySlot_.y + toybox::kBoardFrame;
  return grid;
}

int BattleshipLocalPlayActivity::cellAt(const GridGeometry& grid, const int x, const int y) {
  if (grid.cell <= 0 || x < grid.originX || y < grid.originY) return -1;
  const int col = (x - grid.originX) / grid.cell;
  const int row = (y - grid.originY) / grid.cell;
  if (col < 0 || col >= bship::kSize || row < 0 || row >= bship::kSize) return -1;
  return bship::cellOf(row, col);
}

Rect BattleshipLocalPlayActivity::cellRect(const GridGeometry& grid, const int cell) {
  return Rect{grid.originX + bship::colOf(cell) * grid.cell, grid.originY + bship::rowOf(cell) * grid.cell, grid.cell,
              grid.cell};
}

Rect BattleshipLocalPlayActivity::x4PlaceRosterRect() const {
  const GridGeometry grid = x4PlaceGrid();
  const int top = grid.originY + grid.cell * bship::kSize + toybox::kBoardFrame + toybox::kGutter;
  const int bottom = x4BodySlot_.y + x4BodySlot_.height;
  return Rect{x4BodySlot_.x, top, x4BodySlot_.width, bottom - top};
}

int BattleshipLocalPlayActivity::x4PlaceRosterRowAt(const int x, const int y) const {
  const Rect box = x4PlaceRosterRect();
  const int rowHeight = box.height / bship::kShipCount;
  if (rowHeight < 12 || x < box.x || x >= box.x + box.width || y < box.y) return -1;
  const int row = (y - box.y) / rowHeight;
  return row >= 0 && row < bship::kShipCount ? row : -1;
}

void BattleshipLocalPlayActivity::shuffleX4Fleet() {
  bship::randomFleet(x4Fleet_, seed_);
  selectedX4Ship_ = -1;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP A SHIP TO MOVE IT");
  requestUpdate();
}

void BattleshipLocalPlayActivity::selectX4Ship(const int shipIndex) {
  selectedX4Ship_ = shipIndex;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP A CELL, OR THE SHIP TO TURN");
  requestUpdate();
}

void BattleshipLocalPlayActivity::rotateX4Ship() {
  if (selectedX4Ship_ < 0) return;
  const bship::Ship current = x4Fleet_.ships[selectedX4Ship_];
  bship::Ship candidate = current;
  candidate.horizontal = static_cast<uint8_t>(current.horizontal != 0 ? 0 : 1);
  if (bship::canPlace(x4Fleet_, selectedX4Ship_, candidate)) {
    x4Fleet_.ships[selectedX4Ship_] = candidate;
    requestUpdate();
    return;
  }

  const int length = bship::kShipLength[selectedX4Ship_];
  for (int back = 1; back < length; ++back) {
    bship::Ship shifted = candidate;
    const int row = bship::rowOf(current.bow) - (candidate.horizontal != 0 ? 0 : back);
    const int col = bship::colOf(current.bow) - (candidate.horizontal != 0 ? back : 0);
    if (row < 0 || col < 0) break;
    shifted.bow = static_cast<uint8_t>(bship::cellOf(row, col));
    if (!bship::canPlace(x4Fleet_, selectedX4Ship_, shifted)) continue;
    x4Fleet_.ships[selectedX4Ship_] = shifted;
    requestUpdate();
    return;
  }

  std::snprintf(x4Status_, sizeof(x4Status_), "IT WILL NOT TURN THERE");
  requestUpdate();
}

void BattleshipLocalPlayActivity::moveX4Ship(const int cell) {
  if (selectedX4Ship_ < 0) return;
  bship::Ship candidate = x4Fleet_.ships[selectedX4Ship_];
  candidate.bow = static_cast<uint8_t>(cell);
  if (!bship::canPlace(x4Fleet_, selectedX4Ship_, candidate)) {
    std::snprintf(x4Status_, sizeof(x4Status_), "IT WILL NOT FIT THERE");
    requestUpdate();
    return;
  }
  x4Fleet_.ships[selectedX4Ship_] = candidate;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP A CELL, OR IT AGAIN TO TURN");
  requestUpdate();
}

void BattleshipLocalPlayActivity::handleX4PlaceTap(const int cell) {
  const int ship = bship::shipAt(x4Fleet_, cell);
  if (ship >= 0) {
    if (ship == selectedX4Ship_) {
      rotateX4Ship();
      return;
    }
    selectX4Ship(ship);
    return;
  }
  if (selectedX4Ship_ < 0) {
    std::snprintf(x4Status_, sizeof(x4Status_), "TAP A SHIP TO MOVE IT");
    requestUpdate();
    return;
  }
  moveX4Ship(cell);
}

void BattleshipLocalPlayActivity::routeChoiceInput() {
  namespace fui = freeink::ui;

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady_) return;

  const fui::ActionEvent event = interactions_.route(input);
  if (stage_ == Stage::Transport && event.action == bshipui::ActionLocalPlayRow) {
    chooseTransport(event.value);
    return;
  }
  if (stage_ == Stage::Network && event.action == kActionNetworkRow) {
    chooseNetwork(event.value);
  }
}

void BattleshipLocalPlayActivity::routeX4PlacementInput() {
  namespace fui = freeink::ui;

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  if (interactionsReady_) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions_.route(input);
    if (event.action == bshipui::ActionShuffle) {
      shuffleX4Fleet();
      return;
    }
    if (event.action == bshipui::ActionReady) {
      commitX4Fleet();
      return;
    }
  }

  const int cell = cellAt(x4PlaceGrid(), tapX, tapY);
  if (cell >= 0) {
    handleX4PlaceTap(cell);
    return;
  }
  const int row = x4PlaceRosterRowAt(tapX, tapY);
  if (row >= 0) selectX4Ship(row);
}

void BattleshipLocalPlayActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (stage_ == Stage::BrowserWaiting || stage_ == Stage::X4Placement || stage_ == Stage::Playing) {
      cleanupBrowserNetwork();
      stage_ = Stage::Network;
      selected_ = 0;
      requestUpdate();
      return;
    }
    if (stage_ == Stage::Network) {
      cleanupBrowserNetwork();
      stage_ = Stage::Transport;
      selected_ = 0;
      requestUpdate();
      return;
    }
    finishCancelled();
    return;
  }

  if (stage_ == Stage::BrowserWaiting || stage_ == Stage::X4Placement || stage_ == Stage::Playing) {
    browser_.loop();
    browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
    if (browser_.clientSeen() != lastClientSeen_) {
      lastClientSeen_ = browser_.clientSeen();
      requestUpdate();
    }
    if (stage_ == Stage::X4Placement) routeX4PlacementInput();
    return;
  }

  routeChoiceInput();
}

void BattleshipLocalPlayActivity::drawTransportChoice() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  bshipui::LocalPlayModel model;
  model.selected = selected_;
  bshipui::buildLocalPlayMenu(screen, model);
  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Battleship local play");
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::drawNetworkChoice() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  buildNetworkChoice(screen, selected_);
  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Battleship browser network");
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::drawBrowserWaiting() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, "BATTLESHIP", nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, width, metrics.tabBarHeight},
                    browserPlayer_.view().ready     ? "BROWSER FLEET READY"
                    : browserPlayer_.view().profile ? "BROWSER PLACING SHIPS"
                    : browser_.hotspot()            ? "BROWSER · HOTSPOT"
                                                    : "BROWSER · WI-FI");

  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  renderer.drawCenteredText(UI_10_FONT_ID, y,
                            browserPlayer_.view().profile ? browserPlayer_.name()
                            : browser_.clientSeen()       ? "BROWSER CONNECTED"
                                                          : "WAITING FOR BROWSER",
                            true, EpdFontFamily::BOLD);
  int statusHeight = renderer.getLineHeight(UI_10_FONT_ID);
  if (browserPlayer_.view().profile) {
    auto target = toybox::makeTarget(renderer);
    const int16_t pixels = player::avatarPixels(player::AvatarSize::Row);
    player::drawAvatar(
        target, fui::Rect{static_cast<int16_t>(metrics.verticalSpacing), static_cast<int16_t>(y), pixels, pixels},
        browserPlayer_.name(), player::AvatarSize::Row);
    if (pixels > statusHeight) statusHeight = pixels;
  }
  y += statusHeight + metrics.verticalSpacing;

  const std::string ipUrl = std::string("http://") + browser_.ip() + "/battleship";
  if (browser_.hotspot()) {
    const int gap = 18;
    const int left = (width - (2 * kQrSize + gap)) / 2;
    const std::string wifiQr = std::string("WIFI:T:nopass;S:") + browser_.ssid() + ";;";
    QrUtils::drawQrCode(renderer, Rect{left, y, kQrSize, kQrSize}, wifiQr);
    QrUtils::drawQrCode(renderer, Rect{left + kQrSize + gap, y, kQrSize, kQrSize}, browser_.url());
    y += kQrSize + metrics.verticalSpacing;
    renderer.drawText(SMALL_FONT_ID, left, y, "1. JOIN WI-FI", true);
    renderer.drawText(SMALL_FONT_ID, left + kQrSize + gap, y, "2. OPEN GAME", true);
    y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
    renderer.drawCenteredText(SMALL_FONT_ID, y, browser_.ssid(), true);
  } else {
    const int left = (width - kQrSize) / 2;
    QrUtils::drawQrCode(renderer, Rect{left, y, kQrSize, kQrSize}, browser_.url());
    y += kQrSize + metrics.verticalSpacing;
    renderer.drawCenteredText(SMALL_FONT_ID, y, "OPEN GAME", true);
  }

  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  renderer.drawCenteredText(SMALL_FONT_ID, y, browser_.url(), true);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing / 2;
  renderer.drawCenteredText(SMALL_FONT_ID, y, ipUrl.c_str(), true);

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::drawX4PlaceGrid() {
  const GridGeometry grid = x4PlaceGrid();
  const int side = grid.cell * bship::kSize;
  for (int i = 1; i < bship::kSize; ++i) {
    renderer.fillRect(grid.originX + i * grid.cell, grid.originY, toybox::kHairline, side, true);
    renderer.fillRect(grid.originX, grid.originY + i * grid.cell, side, toybox::kHairline, true);
  }
  renderer.drawRect(grid.originX - toybox::kFrame, grid.originY - toybox::kFrame, side + 2 * toybox::kFrame,
                    side + 2 * toybox::kFrame, toybox::kFrame, true);
  toybox::cornerMarks(renderer,
                      Rect{grid.originX - toybox::kBoardFrame, grid.originY - toybox::kBoardFrame,
                           side + 2 * toybox::kBoardFrame, side + 2 * toybox::kBoardFrame},
                      toybox::kGutter * 2, toybox::kFrame);

  for (int i = 0; i < bship::kShipCount; ++i) {
    const bship::Ship& ship = x4Fleet_.ships[i];
    const int length = bship::kShipLength[i];
    const Rect bow = cellRect(grid, ship.bow);
    const Rect hull{bow.x, bow.y, ship.horizontal != 0 ? grid.cell * length : grid.cell,
                    ship.horizontal != 0 ? grid.cell : grid.cell * length};
    renderer.fillRectDither(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4, LightGray);
    renderer.drawRect(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4,
                      i == selectedX4Ship_ ? toybox::kFrame : toybox::kHairline, true);
    if (i == selectedX4Ship_) toybox::cornerMarks(renderer, hull, std::max(10, grid.cell / 3), toybox::kFrame);
  }
}

void BattleshipLocalPlayActivity::drawX4PlaceRoster() {
  const Rect box = x4PlaceRosterRect();
  const int rowHeight = box.height / bship::kShipCount;
  if (rowHeight < 12) return;

  for (int i = 0; i < bship::kShipCount; ++i) {
    const int y = box.y + i * rowHeight;
    if (i == selectedX4Ship_) {
      renderer.fillRectDither(box.x, y, box.width, rowHeight - 2, LightGray);
      renderer.drawRect(box.x, y, box.width, rowHeight - 2, toybox::kRule, true);
    }
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, box.x + toybox::kGutter / 2, y, rowHeight - 2,
                             bship::shipName(i), true);
  }
}

void BattleshipLocalPlayActivity::drawX4Placement() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  bshipui::PlaceModel model;
  model.status = x4Status_;
  model.canEdit = true;
  const fui::Rect slot = bshipui::buildPlaceChrome(screen, model);
  x4BodySlot_ = Rect{slot.x, slot.y, slot.width, slot.height};
  drawX4PlaceGrid();
  drawX4PlaceRoster();
  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Battleship browser X4 placement");
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::drawPlaying() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, "BATTLESHIP", nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, width, metrics.tabBarHeight},
                    "BOTH FLEETS READY");
  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 4;
  renderer.drawCenteredText(UI_10_FONT_ID, y, browserGame_.turn == 1 ? "BROWSER MOVE" : "YOUR MOVE", true,
                            EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
  renderer.drawCenteredText(SMALL_FONT_ID, y, "GAME BOARD IS THE NEXT CROSSPLAY STEP", true);
  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::render(RenderLock&&) {
  switch (stage_) {
    case Stage::Transport:
      drawTransportChoice();
      break;
    case Stage::Network:
      drawNetworkChoice();
      break;
    case Stage::BrowserWaiting:
      drawBrowserWaiting();
      break;
    case Stage::X4Placement:
      drawX4Placement();
      break;
    case Stage::Playing:
      drawPlaying();
      break;
  }
}
