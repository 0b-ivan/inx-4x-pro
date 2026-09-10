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
constexpr int kFleetCell = 16;
constexpr int kMissRing = 3;

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
  screen.list(list, static_cast<int16_t>(2 * toybox::kRowHeight + toybox::kGutter / 2 + toybox::kGutter),
              fui::LayoutAnchor::Bottom);
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
        if (!accepted) return false;

        if (command.kind == bshipweb::CommandKind::Rematch) {
          activity.selectedX4Ship_ = -1;
          activity.x4AimCell_ = -1;
          activity.seenLastShot_ = 0;
          activity.x4Ready_ = false;
          activity.x4Status_[0] = '\0';
          activity.x4Report_[0] = '\0';
          activity.startX4Placement();
        } else if (command.kind == bshipweb::CommandKind::Profile && activity.stage_ == Stage::BrowserWaiting) {
          activity.startX4Placement();
        } else if (view.ready && activity.stage_ == Stage::X4Placement) {
          if (activity.x4Ready_)
            activity.startMatchIfReady();
          else
            std::snprintf(activity.x4Status_, sizeof(activity.x4Status_), "%s READY - SET YOUR FLEET",
                          activity.browserPlayer_.name());
        }

        if (command.kind == bshipweb::CommandKind::Fire) {
          activity.reportLastShot(true);
          activity.seenLastShot_ = activity.browserGame_.lastShot;
          activity.x4AimCell_ = -1;
        }
        activity.browser_.publish(bshipweb::browserSnapshot(activity.browserGame_, activity.browser_.clientSeen()));
        activity.requestUpdate();
        return true;
      },
      [](void* context) {
        auto& activity = *static_cast<BattleshipLocalPlayActivity*>(context);
        RenderLock lock(activity);
        if (!activity.browserPlayer_.view().ready) {
          activity.browserPlayer_.reset();
          if (activity.stage_ == Stage::X4Placement) {
            activity.stage_ = Stage::BrowserWaiting;
            activity.x4Ready_ = false;
            activity.selectedX4Ship_ = -1;
          }
        }
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
    result.action = 0;
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
  if (index == 0)
    startWifiBrowser();
  else
    startHotspotBrowser();
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
  browserGame_.turn = 1;
  browserPlayer_.reset();
  selectedX4Ship_ = -1;
  x4AimCell_ = -1;
  seenLastShot_ = 0;
  x4Ready_ = false;
  x4Status_[0] = '\0';
  x4Report_[0] = '\0';
  browser_.publish(bshipweb::browserSnapshot(browserGame_, false));
  stage_ = Stage::BrowserWaiting;
  selected_ = 0;
  lastClientSeen_ = browser_.clientSeen();
  requestUpdate();
}

void BattleshipLocalPlayActivity::startX4Placement() {
  bship::randomFleet(x4Fleet_, seed_);
  selectedX4Ship_ = -1;
  x4Ready_ = false;
  std::snprintf(x4Status_, sizeof(x4Status_), "SET YOUR FLEET");
  stage_ = Stage::X4Placement;
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
  requestUpdate();
}

void BattleshipLocalPlayActivity::commitX4Fleet() {
  if (stage_ != Stage::X4Placement || x4Ready_) return;
  x4Ready_ = true;
  selectedX4Ship_ = -1;
  if (browserPlayer_.view().ready) {
    startMatchIfReady();
    return;
  }
  std::snprintf(x4Status_, sizeof(x4Status_), "WAITING FOR %s", browserPlayer_.name());
  requestUpdate();
}

void BattleshipLocalPlayActivity::startMatchIfReady() {
  if (stage_ != Stage::X4Placement || !x4Ready_ || !browserPlayer_.view().ready) return;
  if (!bship::place(browserGame_, 0, x4Fleet_)) {
    LOG_ERR("BSHIPWEB", "X4 fleet was refused after both players became ready");
    x4Ready_ = false;
    std::snprintf(x4Status_, sizeof(x4Status_), "FLEET COULD NOT BE SET");
    requestUpdate();
    return;
  }
  x4AimCell_ = -1;
  x4Report_[0] = '\0';
  std::snprintf(x4Status_, sizeof(x4Status_), "WAITING FOR %s", browserPlayer_.name());
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

BattleshipLocalPlayActivity::GridGeometry BattleshipLocalPlayActivity::targetGrid() const {
  const int fleetBand = kFleetCell * bship::kSize + toybox::kGutter * 2;
  const int room = x4BodySlot_.height - fleetBand;
  GridGeometry grid;
  const int byWidth = (x4BodySlot_.width - 2 * toybox::kBoardFrame) / bship::kSize;
  const int byHeight = (room - 2 * toybox::kBoardFrame) / bship::kSize;
  grid.cell = std::clamp(std::min(byWidth, byHeight), 8, 44);
  grid.originX = x4BodySlot_.x + (x4BodySlot_.width - grid.cell * bship::kSize) / 2;
  grid.originY = x4BodySlot_.y + toybox::kBoardFrame;
  return grid;
}

BattleshipLocalPlayActivity::GridGeometry BattleshipLocalPlayActivity::ownFleetGrid() const {
  const GridGeometry target = targetGrid();
  GridGeometry grid;
  grid.cell = kFleetCell;
  grid.originX = x4BodySlot_.x + toybox::kFrame;
  grid.originY = target.originY + target.cell * bship::kSize + toybox::kBoardFrame + toybox::kGutter;
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
  if (x4Ready_) return;
  bship::randomFleet(x4Fleet_, seed_);
  selectedX4Ship_ = -1;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP A SHIP TO MOVE IT");
  requestUpdate();
}

void BattleshipLocalPlayActivity::selectX4Ship(const int shipIndex) {
  if (x4Ready_) return;
  selectedX4Ship_ = shipIndex;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP A CELL, OR THE SHIP TO TURN");
  requestUpdate();
}

void BattleshipLocalPlayActivity::rotateX4Ship() {
  if (x4Ready_ || selectedX4Ship_ < 0) return;
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
  if (x4Ready_ || selectedX4Ship_ < 0) return;
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
  if (x4Ready_) return;
  const int ship = bship::shipAt(x4Fleet_, cell);
  if (ship >= 0) {
    if (ship == selectedX4Ship_)
      rotateX4Ship();
    else
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

void BattleshipLocalPlayActivity::aimX4Shot(const int cell) {
  if (stage_ != Stage::Playing || bship::over(browserGame_) || browserGame_.turn != 0) return;
  if (bship::shotAt(browserGame_.side[1], cell)) {
    std::snprintf(x4Report_, sizeof(x4Report_), "ALREADY FIRED THERE");
    requestUpdate();
    return;
  }
  if (x4AimCell_ == cell) {
    fireX4Shot();
    return;
  }
  x4AimCell_ = cell;
  std::snprintf(x4Status_, sizeof(x4Status_), "TAP AGAIN TO FIRE");
  requestUpdate();
}

void BattleshipLocalPlayActivity::fireX4Shot() {
  if (x4AimCell_ < 0 || browserGame_.turn != 0 || bship::over(browserGame_)) return;
  if (!bship::fire(browserGame_, x4AimCell_)) {
    LOG_ERR("BSHIPWEB", "X4 shot was refused by BattleshipCore");
    return;
  }
  reportLastShot(false);
  seenLastShot_ = browserGame_.lastShot;
  x4AimCell_ = -1;
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
  requestUpdate();
}

void BattleshipLocalPlayActivity::reportLastShot(const bool browserShot) {
  if (!browserGame_.lastShot) return;
  char where[4] = {};
  bship::cellName(browserGame_.lastShot - 1, where);
  const int sank = bship::lastShotSank(browserGame_);
  const bool hit = bship::lastShotHit(browserGame_);
  if (browserShot) {
    if (sank >= 0)
      std::snprintf(x4Report_, sizeof(x4Report_), "%s SANK YOUR %s", browserPlayer_.name(), bship::shipName(sank));
    else
      std::snprintf(x4Report_, sizeof(x4Report_), hit ? "%s HIT YOU AT %s" : "%s MISSED AT %s", browserPlayer_.name(), where);
  } else {
    if (sank >= 0)
      std::snprintf(x4Report_, sizeof(x4Report_), "YOU SANK THEIR %s", bship::shipName(sank));
    else
      std::snprintf(x4Report_, sizeof(x4Report_), hit ? "%s: HIT" : "%s: MISS", where);
  }
  if (bship::over(browserGame_))
    std::snprintf(x4Status_, sizeof(x4Status_), bship::winner(browserGame_) == 0 ? "YOU WIN" : "%s WINS", browserPlayer_.name());
  else if (browserGame_.turn == 0)
    std::snprintf(x4Status_, sizeof(x4Status_), "YOUR MOVE");
  else
    std::snprintf(x4Status_, sizeof(x4Status_), "WAITING FOR %s", browserPlayer_.name());
}

void BattleshipLocalPlayActivity::routeChoiceInput() {
  fui::InputSnapshot input;
  int tapX = 0, tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady_) return;
  const fui::ActionEvent event = interactions_.route(input);
  if (stage_ == Stage::Transport && event.action == bshipui::ActionLocalPlayRow) chooseTransport(event.value);
  if (stage_ == Stage::Network && event.action == kActionNetworkRow) chooseNetwork(event.value);
}

void BattleshipLocalPlayActivity::routeX4PlacementInput() {
  if (x4Ready_) return;
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (interactionsReady_) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions_.route(input);
    if (event.action == bshipui::ActionShuffle) return shuffleX4Fleet();
    if (event.action == bshipui::ActionReady) return commitX4Fleet();
  }
  const int cell = cellAt(x4PlaceGrid(), tapX, tapY);
  if (cell >= 0) return handleX4PlaceTap(cell);
  const int row = x4PlaceRosterRowAt(tapX, tapY);
  if (row >= 0) selectX4Ship(row);
}

void BattleshipLocalPlayActivity::routePlayingInput() {
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (interactionsReady_) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions_.route(input);
    if (event.action == bshipui::ActionFire) {
      fireX4Shot();
      return;
    }
  }
  const int cell = cellAt(targetGrid(), tapX, tapY);
  if (cell >= 0) aimX4Shot(cell);
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
    if (browserGame_.lastShot && browserGame_.lastShot != seenLastShot_ && stage_ == Stage::Playing) {
      reportLastShot(browserGame_.turn == 0);
      seenLastShot_ = browserGame_.lastShot;
      requestUpdate();
    }
    if (browser_.clientSeen() != lastClientSeen_) {
      lastClientSeen_ = browser_.clientSeen();
      requestUpdate();
    }
    if (stage_ == Stage::X4Placement) routeX4PlacementInput();
    if (stage_ == Stage::Playing) routePlayingInput();
    return;
  }
  routeChoiceInput();
}

void BattleshipLocalPlayActivity::drawTransportChoice() {
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
                    browserPlayer_.view().profile ? "BROWSER PLACING SHIPS" : browser_.hotspot() ? "BROWSER · HOTSPOT" : "BROWSER · WI-FI");
  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;
  renderer.drawCenteredText(UI_10_FONT_ID, y,
                            browserPlayer_.view().profile ? browserPlayer_.name() : browser_.clientSeen() ? "BROWSER CONNECTED" : "WAITING FOR BROWSER",
                            true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
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
  } else {
    const int left = (width - kQrSize) / 2;
    QrUtils::drawQrCode(renderer, Rect{left, y, kQrSize, kQrSize}, browser_.url());
    y += kQrSize + metrics.verticalSpacing;
    renderer.drawCenteredText(SMALL_FONT_ID, y, "OPEN GAME", true);
  }
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
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
  for (int i = 0; i < bship::kShipCount; ++i) {
    const bship::Ship& ship = x4Fleet_.ships[i];
    const Rect bow = cellRect(grid, ship.bow);
    const int length = bship::kShipLength[i];
    const Rect hull{bow.x, bow.y, ship.horizontal ? grid.cell * length : grid.cell, ship.horizontal ? grid.cell : grid.cell * length};
    renderer.fillRectDither(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4, LightGray);
    renderer.drawRect(hull.x + 2, hull.y + 2, hull.width - 4, hull.height - 4,
                      i == selectedX4Ship_ ? toybox::kFrame : toybox::kHairline, true);
  }
}

void BattleshipLocalPlayActivity::drawX4PlaceRoster() {
  const Rect box = x4PlaceRosterRect();
  const int rowHeight = box.height / bship::kShipCount;
  if (rowHeight < 12) return;
  for (int i = 0; i < bship::kShipCount; ++i) {
    const int y = box.y + i * rowHeight;
    if (i == selectedX4Ship_) renderer.fillRectDither(box.x, y, box.width, rowHeight - 2, LightGray);
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, box.x + toybox::kGutter / 2, y, rowHeight - 2,
                             bship::shipName(i), true);
  }
}

void BattleshipLocalPlayActivity::drawX4Placement() {
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  bshipui::PlaceModel model;
  model.status = x4Status_;
  model.canEdit = !x4Ready_;
  const fui::Rect slot = bshipui::buildPlaceChrome(screen, model);
  x4BodySlot_ = Rect{slot.x, slot.y, slot.width, slot.height};
  drawX4PlaceGrid();
  drawX4PlaceRoster();
  interactionsReady_ = !x4Ready_;
  toybox::reportOverflow(interactions_, "Battleship browser X4 placement");
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::drawTargetGrid() {
  const GridGeometry grid = targetGrid();
  const int side = grid.cell * bship::kSize;
  for (int i = 1; i < bship::kSize; ++i) {
    renderer.fillRect(grid.originX + i * grid.cell, grid.originY, toybox::kHairline, side, true);
    renderer.fillRect(grid.originX, grid.originY + i * grid.cell, side, toybox::kHairline, true);
  }
  renderer.drawRect(grid.originX - toybox::kFrame, grid.originY - toybox::kFrame, side + 2 * toybox::kFrame,
                    side + 2 * toybox::kFrame, toybox::kFrame, true);
  const bship::Side& theirs = browserGame_.side[1];
  for (int cell = 0; cell < bship::kCells; ++cell) {
    if (!bship::shotAt(theirs, cell)) continue;
    const Rect box = cellRect(grid, cell);
    const int ship = bship::shipAt(theirs.fleet, cell);
    if (ship < 0) {
      const int outer = std::max(10, box.width / 3);
      const int inner = std::max(4, outer - 2 * kMissRing);
      renderer.fillRoundedRect(box.x + (box.width - outer) / 2, box.y + (box.height - outer) / 2, outer, outer, outer / 2, Black);
      renderer.fillRoundedRect(box.x + (box.width - inner) / 2, box.y + (box.height - inner) / 2, inner, inner, inner / 2, White);
    } else if (bship::sunk(theirs, ship)) {
      renderer.fillRect(box.x, box.y, box.width, box.height, true);
    } else {
      const int peg = std::max(12, box.width / 2);
      renderer.fillRoundedRect(box.x + (box.width - peg) / 2, box.y + (box.height - peg) / 2, peg, peg, 3, Black);
    }
  }
  if (x4AimCell_ >= 0 && !bship::over(browserGame_)) toybox::cornerMarks(renderer, cellRect(grid, x4AimCell_), std::max(10, grid.cell / 3), toybox::kFrame);
}

void BattleshipLocalPlayActivity::drawOwnFleetGrid() {
  const GridGeometry grid = ownFleetGrid();
  const bship::Side& mine = browserGame_.side[0];
  for (int cell = 0; cell < bship::kCells; ++cell) {
    const Rect box = cellRect(grid, cell);
    const int ship = bship::shipAt(mine.fleet, cell);
    if (ship >= 0) renderer.fillRectDither(box.x, box.y, box.width, box.height, LightGray);
    if (!bship::shotAt(mine, cell)) continue;
    if (ship >= 0)
      renderer.fillRect(box.x + 1, box.y + 1, box.width - 2, box.height - 2, true);
    else {
      const int dot = std::max(4, box.width / 4);
      renderer.fillRoundedRect(box.x + (box.width - dot) / 2, box.y + (box.height - dot) / 2, dot, dot, dot / 2, Black);
    }
  }
  const int side = grid.cell * bship::kSize;
  for (int i = 1; i < bship::kSize; ++i) {
    renderer.fillRect(grid.originX + i * grid.cell, grid.originY, toybox::kHairline, side, true);
    renderer.fillRect(grid.originX, grid.originY + i * grid.cell, side, toybox::kHairline, true);
  }
  renderer.drawRect(grid.originX - 1, grid.originY - 1, side + 2, side + 2, toybox::kHairline, true);
}

void BattleshipLocalPlayActivity::drawPlaying() {
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);
  bshipui::BoardModel model;
  model.report = x4Report_;
  char waiting[48] = {};
  if (bship::over(browserGame_))
    model.status = bship::winner(browserGame_) == 0 ? "YOU WIN" : "THEY WIN";
  else if (browserGame_.turn != 0) {
    std::snprintf(waiting, sizeof(waiting), "WAITING FOR %s", browserPlayer_.name());
    model.status = waiting;
  } else if (x4AimCell_ < 0)
    model.status = "TAP A TARGET";
  else
    model.status = "TAP AGAIN TO FIRE";
  model.canFire = browserGame_.turn == 0 && x4AimCell_ >= 0 && !bship::over(browserGame_);
  model.gameOver = bship::over(browserGame_);
  model.theirName = browserPlayer_.name();
  const fui::Rect slot = bshipui::buildBoardChrome(screen, model);
  x4BodySlot_ = Rect{slot.x, slot.y, slot.width, slot.height};
  drawTargetGrid();
  drawOwnFleetGrid();
  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Battleship browser match");
  renderer.displayBuffer();
}

void BattleshipLocalPlayActivity::render(RenderLock&&) {
  switch (stage_) {
    case Stage::Transport: drawTransportChoice(); break;
    case Stage::Network: drawNetworkChoice(); break;
    case Stage::BrowserWaiting: drawBrowserWaiting(); break;
    case Stage::X4Placement: drawX4Placement(); break;
    case Stage::Playing: drawPlaying(); break;
  }
}
