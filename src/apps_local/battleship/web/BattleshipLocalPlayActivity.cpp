#include "BattleshipLocalPlayActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>

#include "../../../DevMode.h"
#include "../../../activities/network/WifiSelectionActivity.h"
#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../../util/QrUtils.h"
#include "../../player/PlayerAvatar.h"
#include "../../ui/Toybox.h"
#include "../../ui/ToyboxFonts.h"
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
  stage_ = Stage::Transport;
  selected_ = 0;
  browser_.setCommands(
      this,
      [](void* context, const bshipweb::Command& command, bshipweb::PlacementView& view) {
        auto& activity = *static_cast<BattleshipLocalPlayActivity*>(context);
        RenderLock lock(activity);
        const bool accepted = activity.browserPlayer_.apply(activity.browserGame_, command);
        view = activity.browserPlayer_.view();
        if (accepted) activity.requestUpdate();
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
  browser_.publish(bshipweb::browserSnapshot(browserGame_, false));
  stage_ = Stage::BrowserWaiting;
  selected_ = 0;
  lastClientSeen_ = browser_.clientSeen();
  requestUpdate();
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

void BattleshipLocalPlayActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (stage_ == Stage::BrowserWaiting) {
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

  if (stage_ == Stage::BrowserWaiting) {
    browser_.loop();
    browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
    if (browser_.clientSeen() != lastClientSeen_) {
      lastClientSeen_ = browser_.clientSeen();
      requestUpdate();
    }
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
  }
}
