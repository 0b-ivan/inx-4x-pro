#pragma once

// X4 host/Game Master for Cards Against Whatever. The phone is ONLY a
// controller; the authoritative game never leaves this Activity.
#include <memory>

#include "../../activities/Activity.h"
#include "CardPartyCategories.h"
#include "CardPartyCore.h"
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
  enum class Stage : uint8_t { Choice, Decks, Categories, Hosted, Failed };
  // One pack file on the card, as the deck list shows it.
  struct DeckEntry {
    char path[80];
    char file[48];
    cardparty::sdpack::Info info;
    bool readable;
  };
  static constexpr uint8_t kMaxDecks = 9;
  void resetGame();
  void loadConfiguredDeck();
  bool loadPack(const char* path);
  void scanDecks();
  void chooseDeck(int row);
  void saveDeckChoice();
  void drawDecks();
  void applyCategories();
  void loadSelection();
  void saveSelection();
  void leaveCategories();
  void drawCategories();
  void start(cardparty::web::Server::Mode mode);
  void chooseExistingWifi();
  void releaseNetwork();
  void handleTap(int x, int y);
  void drawBase(const char* section);
  void drawChoice();
  void drawLobby();
  void drawGame();
  void drawError();
  int drawWrapped(int fontId, const char* text, int x, int y, int maxWidth, int lineStep, int maxRows,
                  bool draw = true);
  void drawDiagnostics();
  bool matchInProgress() const;
  void leave();

  cardparty::Game game_;
  cardparty::Sessions sessions_;
  cardparty::sdpack::Pack sdPack_;
  cardparty::Deck activeDeck_ = cardparty::demoDeck();
  // The pack's cards that pass the host's category switches.
  cardparty::categories::FilteredDeck filtered_;
  bool packLoaded_ = false;
  // File of the loaded pack; empty while the built-in demo plays.
  char deckPath_[80] = {};
  DeckEntry decks_[kMaxDecks] = {};
  uint8_t deckCount_ = 0;
  bool deckPlayable_ = true;
  uint16_t enabled_ = 0, savedEnabled_ = 0;
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
