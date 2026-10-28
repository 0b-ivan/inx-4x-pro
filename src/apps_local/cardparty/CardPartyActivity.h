#pragma once

// X4 host/Game Master for Cards Against Whatever. The phone is ONLY a
// controller; the authoritative game never leaves this Activity.
#include <memory>

#include "../../activities/Activity.h"
#include "CardPartyCatalog.h"
#include "CardPartyCategories.h"
#include "CardPartyCore.h"
#include "CardPartyHistory.h"
#include "CardPartyDemoDeck.h"
#include "CardPartySdPack.h"
#include "CardPartySessions.h"
#include "web/CardPartyBrowserServer.h"

class CardPartyActivity final : public Activity {
 public:
  CardPartyActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("CardParty", renderer, input), sessions_(game_), server_(game_, sessions_, activeDeck_) {}
  ~CardPartyActivity() override = default;
  static std::unique_ptr<Activity> create(GfxRenderer&, MappedInputManager&);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return server_.running(); }
  bool skipLoopDelay() override { return server_.running(); }

 private:
  enum class Stage : uint8_t { Choice, Decks, Remote, Categories, Rules, Menu, Players, Hosted, Failed };
  // Blocking network work, run from loop() after the screen says so.
  enum class RemoteJob : uint8_t { None, Catalog, Pack };
  // One pack file on the card, as the deck list shows it.
  struct DeckEntry {
    char path[80];
    char file[48];
    cardparty::sdpack::Info info;
    bool readable;
  };
  static constexpr uint8_t kMaxDecks = 8;
  void resetGame();
  void loadConfiguredDeck();
  bool loadPack(const char* path);
  void scanDecks();
  void chooseDeck(int row);
  void saveDeckChoice();
  void drawDecks();
  void openRemote();
  void runRemoteJob();
  void fetchCatalog();
  void installPack(uint8_t index);
  void leaveRemote();
  void drawRemote();
  void applyCategories();
  void toggleCategory(uint8_t index);
  // Server running: lobby or match. Setup screens then never reset the game.
  bool hosting() const { return server_.running(); }
  void loadRules();
  void saveRules();
  void loadHistory();
  void saveHistory();
  void cycleRule(uint8_t row);
  void leaveRules();
  void drawRules();
  void menuTap(uint8_t row);
  void drawMenu();
  void playersTap(int row);
  void drawPlayers();
  void drawFinal();
  void hostAction(bool menu);
  void loadSelection();
  void saveSelection();
  void leaveCategories();
  void drawCategories();
  void start(cardparty::web::Server::Mode mode);
  void chooseExistingWifi();
  // Raises station mode and opens the system picker; onReady runs once
  // connected, a cancel returns to `back`.
  void connectWifi(Stage back, void (CardPartyActivity::*onReady)());
  void hostExistingWifi();
  void queueCatalog();
  void releaseNetwork();
  void handleTap(int x, int y);
  void drawBase(const char* section);
  void drawChoice();
  void drawLobby();
  void drawGame();
  void drawError();
  int drawWrapped(int fontId, const char* text, int x, int y, int maxWidth, int lineStep, int maxRows, bool draw = true,
                  bool black = true);
  void drawDiagnostics();
  bool matchInProgress() const;
  void leave();

  cardparty::Game game_;
  cardparty::Sessions sessions_;
  cardparty::sdpack::Pack sdPack_;
  cardparty::Deck activeDeck_ = cardparty::demoDeck();
  bool packLoaded_ = false;
  // File of the loaded pack; empty while the built-in demo plays.
  char deckPath_[80] = {};
  DeckEntry decks_[kMaxDecks] = {};
  uint8_t deckCount_ = 0;
  // Public pack list from GitHub and the job loop() runs next.
  cardparty::catalog::Catalog catalog_;
  RemoteJob job_ = RemoteJob::None;
  uint8_t jobIndex_ = 0;
  bool downloadCancel_ = false;
  const char* busy_ = nullptr;
  bool deckPlayable_ = true;
  uint16_t enabled_ = 0, savedEnabled_ = 0;
  cardparty::Settings settings_{};
  // Two-tap confirmations on the X4: remove a player, end the match.
  uint8_t kickArmed_ = cardparty::kNone;
  uint32_t kickArmedAt_ = 0;
  bool endArmed_ = false;
  uint32_t endArmedAt_ = 0;
  cardparty::history::Book history_;
  uint32_t historySaved_ = 0;
  cardparty::web::Server server_;
  Stage stage_ = Stage::Choice;
  // Station mode raised by the Wi-Fi picker for [02]; dev mode stays paused
  // while this Activity holds the radio.
  bool ownsSta_ = false;
  bool devModePaused_ = false;
  uint32_t fingerprint_ = 0;
  bool abortArmed_ = false;
  uint32_t abortArmedAt_ = 0;
  bool backArmed_ = false;
  uint32_t backArmedAt_ = 0;
  char report_[90] = {};
};
