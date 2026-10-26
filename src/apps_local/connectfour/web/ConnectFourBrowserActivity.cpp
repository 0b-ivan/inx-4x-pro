#include "ConnectFourBrowserActivity.h"

#include <Memory.h>
#include <WiFi.h>
#include <cstdio>
#include <string>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../../util/QrUtils.h"
#include "../../ui/Toybox.h"
#include "../../ui/ToyboxFonts.h"
#include "../../ui/ToyboxTheme.h"
#include "../ConnectFourScreens.h"

namespace c4 = connectfour;
namespace fui = freeink::ui;

void ConnectFourBrowserActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  failed_ = !server_.begin();
  shownJoined_ = false;
  shownConnected_ = false;
  requestUpdate();
}
void ConnectFourBrowserActivity::onExit() {
  server_.stop();
  Activity::onExit();
}
void ConnectFourBrowserActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    server_.stop();
    finish();
    return;
  }
  if (failed_) return;
  // Network callbacks mutate the authoritative board in this loop pass.
  // Hold the same rendering mutex used by Battleship while frames and
  // touch actions update shared state, so the e-ink render sees one revision.
  RenderLock lock(*this);
  server_.loop();
  if (server_.takeChanged() || shownJoined_ != server_.round().joined() ||
      shownConnected_ != server_.clientSeen()) {
    shownJoined_ = server_.round().joined();
    shownConnected_ = server_.clientSeen();
    requestUpdate();
  }
  if (!server_.round().joined()) return;
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  const c4::Game& game = server_.round().game();
  if (!c4::over(game)) {
    if (game.turn != c4::kLight) return;
    const int col = c4ui::columnAt(toybox::makeTarget(renderer).deviceContext(), tapX, tapY);
    if (col >= 0 && server_.deviceDrop(static_cast<uint8_t>(col))) requestUpdate();
    return;
  }
  // Route precisely through the result screen's registered PLAY AGAIN and
  // DONE actions. Never interpret an arbitrary tap near the footer as rematch.
  if (!interactionsReady_) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);
  if (event.action == c4ui::ActionAgain) {
    if (server_.deviceRematch()) requestUpdate();
  } else if (event.action == c4ui::ActionDone) {
    server_.stop();
    finish();
  }
}
void ConnectFourBrowserActivity::drawJoin() {
  interactionsReady_ = false;
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0,m.topPadding,width,m.headerHeight}, "CONNECT FOUR", nullptr);
  GUI.drawSubHeader(renderer, Rect{0,m.topPadding+m.headerHeight,width,m.tabBarHeight},
                    failed_ ? "HOTSPOT FAILED" : "BROWSER · HOTSPOT");
  int y=m.topPadding+m.headerHeight+m.tabBarHeight+m.verticalSpacing*2;
  renderer.drawCenteredText(UI_10_FONT_ID, y, failed_ ? "COULD NOT START WI-FI" :
      server_.clientSeen() ? "BROWSER CONNECTED · JOIN GAME" : "WAITING FOR BROWSER",
      true,EpdFontFamily::BOLD);
  y+=renderer.getLineHeight(UI_10_FONT_ID)+m.verticalSpacing;
  if (!failed_) {
    constexpr int qr=170, gap=18;
    const int left=(width-(2*qr+gap))/2;
    const std::string wifiQr=std::string("WIFI:T:WPA;S:")+server_.ssid()+";P:"+server_.password()+";;";
    QrUtils::drawQrCode(renderer,Rect{left,y,qr,qr},wifiQr);
    QrUtils::drawQrCode(renderer,Rect{left+qr+gap,y,qr,qr},server_.url());
    y+=qr+m.verticalSpacing;
    renderer.drawText(SMALL_FONT_ID,left,y,"1. JOIN WI-FI",true);
    renderer.drawText(SMALL_FONT_ID,left+qr+gap,y,"2. OPEN GAME",true);
    y+=renderer.getLineHeight(SMALL_FONT_ID)+m.verticalSpacing;
    const std::string label=std::string("WI-FI PASS: ")+server_.password();
    renderer.drawCenteredText(SMALL_FONT_ID,y,label.c_str(),true);
    y+=renderer.getLineHeight(SMALL_FONT_ID)+m.verticalSpacing;
    renderer.drawCenteredText(SMALL_FONT_ID,y,server_.url(),true);
  }
  const auto labels=mappedInput.mapLabels("Back","","","");
  GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
  renderer.displayBuffer();
}
void ConnectFourBrowserActivity::drawGame() {
  renderer.clearScreen();
  auto target=toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target,target.deviceContext(),noInput,interactions_);
  toybox::Screen surface(frame);
  const c4::Game& game=server_.round().game();
  if (!c4::over(game)) {
    c4ui::BoardModel model;
    model.game=game;
    model.open=c4::openColumns(game);
    model.seat=c4::kLight;
    model.yourTurn=game.turn==c4::kLight;
    model.opponentName="BROWSER";
    c4ui::buildBoard(surface,model);
  } else {
    c4ui::ResultModel model;
    model.game=game;
    model.outcome=game.outcome;
    model.seat=c4::kLight;
    model.opponentName="BROWSER";
    if (game.outcome==c4::Outcome::LightWins) c4::winningLine(game,c4::kLight,model.line);
    else if (game.outcome==c4::Outcome::DarkWins) c4::winningLine(game,c4::kDark,model.line);
    c4ui::buildResult(surface,model);
  }
  interactionsReady_ = true;
  const auto labels=mappedInput.mapLabels("Back","","","");
  GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
  renderer.displayBuffer();
}
void ConnectFourBrowserActivity::render(RenderLock&&) {
  if (failed_ || !server_.round().joined()) drawJoin();
  else drawGame();
}
