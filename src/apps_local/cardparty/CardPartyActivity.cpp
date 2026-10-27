#include "CardPartyActivity.h"

#include <Memory.h>
#include <Logging.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#ifdef SIMULATOR
#include <cstdlib>
#endif
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../../util/QrUtils.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxSeed.h"

namespace {
namespace cp=cardparty;
constexpr int kGutter=26;
constexpr int kQr=160;
const char* phaseLabel(cp::Phase phase) {
  switch(phase) {
    case cp::Phase::Lobby:return "WAITING FOR PLAYERS";
    case cp::Phase::Playing:return "COLLECTING ANSWERS";
    case cp::Phase::Judging:return "CARD CZAR DECIDES";
    case cp::Phase::Reveal:return "ROUND FINISHED";
    case cp::Phase::Finished:return "GAME OVER";
  }
  return "WAITING";
}
uint32_t visibleFingerprint(const cp::PublicView& v) {
  // Only changes to visible fields trigger e-ink refresh; avoid per-frame
  // repainting for WebSocket housekeeping or repeated unchanged snapshots.
  uint32_t h=2166136261u;
  auto mix=[&](uint32_t x){h=(h ^ x) * 16777619u;};
  mix(static_cast<uint8_t>(v.phase));
  mix(v.round); mix(v.prompt); mix(v.submitted); mix(v.expected);
  mix(v.czar); mix(v.winner); mix(v.selected); mix(v.aborted ? 1u : 0u);
  for(const auto& p:v.players) {
    mix(p.seated?1u:0u);mix(p.connected?1u:0u);mix(p.points);
    for(unsigned char c:p.name)mix(c);
  }
  return h;
}
}

std::unique_ptr<Activity> CardPartyActivity::create(GfxRenderer& renderer, MappedInputManager& input) {
  return makeUniqueNoThrow<CardPartyActivity>(renderer,input);
}
void CardPartyActivity::resetGame() {
  abortArmed_=false;
  abortArmedAt_=0;
  backArmed_=false;
  backArmedAt_=0;
  const auto rules=cp::Rules{5,10,true};
  if(!game_.configure(cp::demoDeck(),rules,toybox::seed())) {
    stage_=Stage::Failed;
    std::snprintf(report_,sizeof(report_),"DEMO DECK INVALID");
  } else {
    stage_=Stage::Choice;
    report_[0]=0;
  }
  sessions_.reset();
  fingerprint_=visibleFingerprint(game_.publicView());
}
void CardPartyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  ownsOrientation_=true;
  resetGame();
#ifdef SIMULATOR
  // SDL fixture mode exercises the actual CardPartyActivity renderer.
  // Real production networking is never enabled in the simulator.
  const char* scenario=std::getenv("CROSSPLAY_SIM_CARDPARTY_STATE");
  if (scenario && *scenario && std::strcmp(scenario,"setup")!=0)
    start(cp::web::Server::Mode::Hotspot);
#endif
  requestUpdate();
}
void CardPartyActivity::onExit() {
  server_.stop();
  if(ownsOrientation_) {
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    ownsOrientation_=false;
  }
  Activity::onExit();
}
void CardPartyActivity::leave() {
  ActivityResult cancelled;
  cancelled.isCancelled=true;
  setResult(std::move(cancelled));
  finish();
}
void CardPartyActivity::start(cp::web::Server::Mode mode) {
  if(!server_.begin(mode)) {
    stage_=Stage::Failed;
    std::snprintf(report_,sizeof(report_),
                  mode==cp::web::Server::Mode::Hotspot ?
                  "HOTSPOT COULD NOT START" : "CONNECT TO WI-FI FIRST");
  } else {
    stage_=Stage::Hosted;
    fingerprint_=visibleFingerprint(game_.publicView());
  }
  requestUpdate();
}
void CardPartyActivity::handleTap(int x,int y) {
  const int height=renderer.getScreenHeight();
  const int width=renderer.getScreenWidth();
  if(stage_==Stage::Choice) {
    if(y>=145&&y<232) start(cp::web::Server::Mode::Hotspot);
    else if(y>=240&&y<325) start(cp::web::Server::Mode::ExistingWifi);
    return;
  }
  if(stage_==Stage::Failed) {
    if(y>=height-100){server_.stop();resetGame();requestUpdate();}
    return;
  }
  if(stage_!=Stage::Hosted||x<0||x>width||y<height-90)return;
  const cp::Phase phase=game_.phase();
  if(phase==cp::Phase::Finished) {
    // Same hotspot, password and seats: phones land in the new lobby.
    abortArmed_=backArmed_=false;
    if(server_.hostRematch()) report_[0]=0;
    else std::snprintf(report_,sizeof(report_),"ACTION NOT AVAILABLE");
    requestUpdate();
    return;
  }
  bool accepted=false;
  backArmed_=false;
  if(phase==cp::Phase::Judging) {
    const uint32_t now=millis();
    if(!abortArmed_ || static_cast<uint32_t>(now-abortArmedAt_)>7000u) {
      abortArmed_=true;
      abortArmedAt_=now;
      std::snprintf(report_,sizeof(report_),"TAP AGAIN TO ABORT ROUND");
      requestUpdate();
      return;
    }
    abortArmed_=false;
    accepted=server_.hostAbort();
  } else {
    abortArmed_=false;
    if(phase==cp::Phase::Lobby) accepted=server_.hostStart();
    else if(phase==cp::Phase::Playing) accepted=server_.hostClose();
    else if(phase==cp::Phase::Reveal) accepted=server_.hostNext();
  }
  if(!accepted) {
    std::snprintf(report_,sizeof(report_),
                  phase==cp::Phase::Lobby ? "NEED 3 CONNECTED PLAYERS" :
                  phase==cp::Phase::Playing ? "NO ANSWERS RECEIVED YET" :
                  "ACTION NOT AVAILABLE");
  } else report_[0]=0;
  requestUpdate();
}
void CardPartyActivity::loop() {
  if(mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if(stage_==Stage::Choice) {leave();return;}
    // One stray press must not end a party for everyone at the table.
    if(stage_==Stage::Hosted && matchInProgress()) {
      const uint32_t now=millis();
      if(!backArmed_ || static_cast<uint32_t>(now-backArmedAt_)>7000u) {
        backArmed_=true;
        backArmedAt_=now;
        std::snprintf(report_,sizeof(report_),"PRESS BACK AGAIN TO END SESSION");
        requestUpdate();
        return;
      }
    }
    server_.stop();
    resetGame();
    requestUpdate();
    return;
  }
  // The websocket callback is allowed to mutate Game. Render task reads
  // snapshots, so serialize the network pump through the Activity render lock.
  if(server_.running()) {
    RenderLock lock(*this);
    server_.loop();
    const uint32_t next=visibleFingerprint(game_.publicView());
    if(next!=fingerprint_) {
      fingerprint_=next;
      requestUpdate();
    }
  }
  int x=0,y=0;
  if(mappedInput.wasScreenTapped(x,y))handleTap(x,y);
}
void CardPartyActivity::drawBase(const char* section) {
  renderer.clearScreen();
  const int width=renderer.getScreenWidth();
  renderer.drawText(UI_10_FONT_ID,kGutter,20,"C / A / W    // CROSSPLAY TERMINAL",true);
  renderer.drawText(SMALL_FONT_ID,width-260,25,section,true);
  renderer.fillRect(kGutter,63,width-2*kGutter,2,true);
}
void CardPartyActivity::drawChoice() {
  drawBase("SETUP");
  renderer.drawText(UI_10_FONT_ID,kGutter,92,"CARDS AGAINST WHATEVER",true);
  renderer.drawText(SMALL_FONT_ID,kGutter,125,"X4 = GAME MASTER  /  PHONES = CARDS",true);
  const int width=renderer.getScreenWidth();
  renderer.drawRect(kGutter,145,width-2*kGutter,86,2,true);
  renderer.drawText(UI_10_FONT_ID,kGutter+20,174,"[01] CREATE HOTSPOT",true);
  renderer.drawText(SMALL_FONT_ID,kGutter+20,205,"LOCAL / PASSWORD PROTECTED",true);
  renderer.drawRect(kGutter,240,width-2*kGutter,85,2,true);
  renderer.drawText(UI_10_FONT_ID,kGutter+20,270,"[02] EXISTING WI-FI",true);
  renderer.drawText(SMALL_FONT_ID,kGutter+20,304,"SAME NETWORK AS PLAYERS",true);
  renderer.drawText(SMALL_FONT_ID,kGutter,renderer.getScreenHeight()-35,"BACK  /  CANCEL",true);
}
void CardPartyActivity::drawLobby() {
  drawBase("JOIN SESSION");
  const int width=renderer.getScreenWidth();
  renderer.drawText(UI_10_FONT_ID,kGutter,90,"WAITING FOR PLAYERS",true);
  int connected=0;
  const cp::PublicView view=game_.publicView();
  for(const auto& p:view.players)if(p.seated&&p.connected)++connected;
  char stats[64]={};
  std::snprintf(stats,sizeof(stats),"CONNECTED: %d / 8    REQUIRED: 3",connected);
  renderer.drawText(SMALL_FONT_ID,kGutter,128,stats,true);

  // 2 QR codes in a WPA2 hotspot: join the private Wi-Fi first, then open
  // the actual game URL in a regular browser (not captive-portal mini UI).
  if(server_.hotspot()) {
    char wifiQr[128]={};
    std::snprintf(wifiQr,sizeof(wifiQr),"WIFI:T:WPA;S:%s;P:%s;;",
                  server_.ssid(),server_.password());
    QrUtils::drawQrCode(renderer,Rect{kGutter,157,kQr,kQr},wifiQr);
    renderer.drawText(SMALL_FONT_ID,kGutter,323,"01 / JOIN WI-FI",true);
    renderer.drawText(SMALL_FONT_ID,kGutter,347,server_.password(),true);
    QrUtils::drawQrCode(renderer,Rect{kGutter+kQr+32,157,kQr,kQr},server_.url());
    renderer.drawText(SMALL_FONT_ID,kGutter+kQr+32,323,"02 / OPEN GAME",true);
  } else {
    QrUtils::drawQrCode(renderer,Rect{kGutter,157,kQr,kQr},server_.url());
    renderer.drawText(SMALL_FONT_ID,kGutter,323,"OPEN GAME",true);
  }
  // Roster and URL are plain text too, so connection works without a camera.
  const int right=std::min(width-230,kGutter+2*kQr+76);
  renderer.drawText(SMALL_FONT_ID,right,170,server_.ssid(),true);
  renderer.drawText(SMALL_FONT_ID,right,200,server_.ip(),true);
  int y=236;
  for(const auto& p:view.players)if(p.seated&&y<350){
    renderer.drawText(SMALL_FONT_ID,right,y,p.name,true);y+=22;
  }
  const int bottom=renderer.getScreenHeight()-72;
  renderer.fillRect(kGutter,bottom-4,width-2*kGutter,2,true);
  renderer.drawText(UI_10_FONT_ID,kGutter,bottom+8,connected>=3?"[ START GAME ]":"[ WAITING FOR 3 PLAYERS ]",true);
  if(report_[0])renderer.drawText(SMALL_FONT_ID,kGutter,bottom+42,report_,true);
  drawDiagnostics();
}
void CardPartyActivity::drawPrompt(const char* sentence,int x,int y,int maxChars) {
  if(!sentence)return;
  char line[112]={};
  int used=0;
  int row=0;
  const char* p=sentence;
  while(*p && row<3) {
    const char* start=p;
    while(*p && *p!=' ')++p;
    const int n=static_cast<int>(p-start);
    if(used>0 && used+1+n>maxChars) {
      renderer.drawText(UI_10_FONT_ID,x,y+row*38,line,true);
      ++row;used=0;line[0]=0;
      if(row==3)break;
    }
    if(used>0)line[used++]=' ';
    const int copy=std::min(n,static_cast<int>(sizeof(line))-used-1);
    std::memcpy(line+used,start,copy);used+=copy;line[used]=0;
    while(*p==' ')++p;
  }
  if(used&&row<3)renderer.drawText(UI_10_FONT_ID,x,y+row*38,line,true);
}
void CardPartyActivity::drawGame() {
  const cp::PublicView v=game_.publicView();
  drawBase(phaseLabel(v.phase));
  const int width=renderer.getScreenWidth();
  char status[96]={};
  const char* czar=v.czar<cp::kPlayers?v.players[v.czar].name:"-";
  std::snprintf(status,sizeof(status),"ROUND %02u / 10     CZAR: %.22s",v.round,czar);
  renderer.drawText(SMALL_FONT_ID,kGutter,94,status,true);
  renderer.fillRect(kGutter,128,width-2*kGutter,1,true);
  renderer.drawText(SMALL_FONT_ID,kGutter,148,"/// PROMPT",true);
  const cp::Deck& deck=cp::demoDeck();
  if(v.prompt<deck.promptsCount) drawPrompt(deck.prompts[v.prompt].text,kGutter,182,58);
  renderer.fillRect(kGutter,318,width-2*kGutter,1,true);
  if(v.phase==cp::Phase::Playing || v.phase==cp::Phase::Judging) {
    std::snprintf(status,sizeof(status),"%u / %u ANSWERS RECEIVED",v.submitted,v.expected);
    renderer.drawText(UI_10_FONT_ID,kGutter,334,status,true);
  } else if(v.phase==cp::Phase::Reveal || v.phase==cp::Phase::Finished) {
    const char* winner=v.winner==cp::kRando?"RANDO CARDRISSIAN":
        v.winner<cp::kPlayers?v.players[v.winner].name:"NO WINNER";
    renderer.drawText(UI_10_FONT_ID,kGutter,334,winner,true);
  }
  const int bottom=renderer.getScreenHeight()-72;
  renderer.fillRect(kGutter,bottom-4,width-2*kGutter,2,true);
  const char* action="[ WAITING ]";
  if(v.phase==cp::Phase::Playing)action="[ CLOSE SUBMISSIONS ]";
  if(v.phase==cp::Phase::Judging)action="[ ABORT STALLED ROUND ]";
  if(v.phase==cp::Phase::Reveal)action="[ NEXT ROUND ]";
  if(v.phase==cp::Phase::Finished)action="[ REMATCH ]   BACK x2 = END";
  renderer.drawText(UI_10_FONT_ID,kGutter,bottom+8,action,true);
  if(report_[0])renderer.drawText(SMALL_FONT_ID,kGutter,bottom+42,report_,true);
  drawDiagnostics();
}
bool CardPartyActivity::matchInProgress() const {
  if(game_.phase()!=cp::Phase::Lobby) return true;
  for(const auto& p:game_.publicView().players) if(p.seated) return true;
  return false;
}
void CardPartyActivity::drawDiagnostics() {
  // Field telemetry for hardware acceptance; refreshed with each repaint,
  // never on its own (no extra e-ink refreshes). Counts only, no secrets.
  const cp::web::Server::Diagnostics d=server_.diagnostics();
  if(!d.valid) return;
  char line[64]={};
  std::snprintf(line,sizeof(line),"SYS H%uK M%uK WS%02u AP%02u",
                static_cast<unsigned>(d.freeHeap/1024),static_cast<unsigned>(d.minFreeHeap/1024),
                d.sockets,d.stations);
  const int width=renderer.getScreenWidth();
  const int x=width-kGutter-renderer.getTextWidth(SMALL_FONT_ID,line);
  renderer.drawText(SMALL_FONT_ID,x,renderer.getScreenHeight()-30,line,true);
}
void CardPartyActivity::drawError() {
  drawBase("ERROR");
  renderer.drawText(UI_10_FONT_ID,kGutter,138,"COULD NOT START SESSION",true);
  renderer.drawText(SMALL_FONT_ID,kGutter,198,report_,true);
  renderer.drawText(UI_10_FONT_ID,kGutter,renderer.getScreenHeight()-76,"[ BACK TO SETUP ]",true);
}
void CardPartyActivity::render(RenderLock&&) {
  switch(stage_) {
    case Stage::Choice:drawChoice();break;
    case Stage::Hosted:
      if(game_.phase()==cp::Phase::Lobby)drawLobby();
      else drawGame();
      break;
    case Stage::Failed:drawError();break;
  }
  renderer.displayBuffer();
}
