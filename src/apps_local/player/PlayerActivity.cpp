#include "PlayerActivity.h"

#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <variant>

#include "../../activities/util/KeyboardEntryActivity.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "PlayerHubScreen.h"
#include "PlayerName.h"
#include "PlayerScreen.h"

namespace {

const char* authMessage(const player::AuthResult result) {
  switch (result) {
    case player::AuthResult::Success:
      return "PLAYER SELECTED";
    case player::AuthResult::WrongPin:
      return "WRONG PIN";
    case player::AuthResult::Locked:
      return "PIN LOCKED UNTIL RESTART";
    case player::AuthResult::InvalidArgument:
      return "PIN MUST BE 4 DIGITS";
    case player::AuthResult::PlayerNotFound:
      return "PLAYER NOT FOUND";
    case player::AuthResult::CredentialMissing:
      return "PLAYER HAS NO PIN";
    case player::AuthResult::StorageError:
      return "PLAYER STORAGE ERROR";
    case player::AuthResult::CryptoError:
      return "PIN CHECK FAILED";
  }
  return "LOGIN FAILED";
}

const char* registrationMessage(const player::PlayerServiceResult result) {
  switch (result) {
    case player::PlayerServiceResult::Ok:
      return "PROFILE SAVED";
    case player::PlayerServiceResult::GuestNotEligible:
      return "PLAY ONE MATCH FIRST";
    case player::PlayerServiceResult::NameTaken:
      return "NAME ALREADY USED";
    case player::PlayerServiceResult::InvalidPin:
      return "PIN MUST BE 4 DIGITS";
    case player::PlayerServiceResult::InvalidArgument:
      return "INVALID PLAYER NAME";
    case player::PlayerServiceResult::RandomUnavailable:
      return "RANDOM SOURCE FAILED";
    case player::PlayerServiceResult::PlayerNotFound:
      return "PLAYER NOT FOUND";
    case player::PlayerServiceResult::CryptoError:
      return "PIN SETUP FAILED";
    case player::PlayerServiceResult::StorageError:
      return "PLAYER STORAGE ERROR";
  }
  return "REGISTRATION FAILED";
}

bool copyTrimmedName(const std::string& input, char* output, const size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  size_t first = 0;
  while (first < input.size() && (input[first] == ' ' || input[first] == '\t')) ++first;
  size_t last = input.size();
  while (last > first && (input[last - 1] == ' ' || input[last - 1] == '\t')) --last;
  const size_t length = last - first;
  if (length == 0 || length >= capacity) return false;
  std::memcpy(output, input.data() + first, length);
  output[length] = '\0';
  return true;
}

}  // namespace

std::unique_ptr<Activity> PlayerActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PlayerActivity>(renderer, mappedInput);
}

void PlayerActivity::setMessage(const char* message) {
  std::snprintf(message_.data(), message_.size(), "%s", message == nullptr ? "" : message);
}

void PlayerActivity::refreshPlayers() {
  playerCount_ = 0;
  for (auto& item : playerItems_) item = freeink::ui::ListItem{};
  if (!player::runtime().ready()) return;

  size_t count = 0;
  const player::StoreResult result = player::runtime().listPlayers(players_.data(), players_.size(), count);
  if (result != player::StoreResult::Ok) {
    setMessage("PLAYER LIST FAILED");
    return;
  }

  playerCount_ = count;
  for (size_t i = 0; i < playerCount_; ++i) {
    playerItems_[i].label = players_[i].name;
    playerItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int PlayerActivity::activePlayerIndex() const {
  const player::Player* active = player::runtime().activePlayer();
  if (active == nullptr) return -1;
  for (size_t i = 0; i < playerCount_; ++i) {
    if (players_[i].id == active->id) return static_cast<int>(i);
  }
  return -1;
}

void PlayerActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!player::runtime().begin()) {
    setMessage("PLAYER DATABASE COULD NOT OPEN");
  }
  refreshPlayers();
  requestUpdate();
}

void PlayerActivity::startLogin(const size_t index) {
  if (!player::runtime().ready() || index >= playerCount_) return;
  pendingLoginId_ = players_[index].id;

  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "ENTER 4-DIGIT PIN", "", 4,
                                                            InputType::Password);
  if (!keyboard) {
    setMessage("NOT ENOUGH MEMORY");
    requestUpdate();
    return;
  }

  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
    if (keyboardResult == nullptr) return;

    const player::AuthResult auth = player::runtime().login(pendingLoginId_, keyboardResult->text.c_str());
    setMessage(authMessage(auth));
    if (auth == player::AuthResult::Success) refreshPlayers();
    requestUpdate();
  });
}

void PlayerActivity::startRegistrationName() {
  if (!player::runtime().ready() || !player::runtime().guest().active()) return;
  if (player::runtime().guest().completedMatches == 0) {
    setMessage("PLAY ONE MATCH FIRST");
    requestUpdate();
    return;
  }

  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "CHOOSE PLAYER NAME", "",
                                                            player::kMaxPlayerNameLength, InputType::Text);
  if (!keyboard) {
    setMessage("NOT ENOUGH MEMORY");
    requestUpdate();
    return;
  }

  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
    if (keyboardResult == nullptr) return;
    if (!copyTrimmedName(keyboardResult->text, pendingName_.data(), pendingName_.size())) {
      setMessage("PLAYER NAME REQUIRED");
      requestUpdate();
      return;
    }
    startRegistrationPin();
  });
}

void PlayerActivity::startRegistrationPin() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "SET 4-DIGIT PIN", "", 4,
                                                            InputType::Password);
  if (!keyboard) {
    setMessage("NOT ENOUGH MEMORY");
    requestUpdate();
    return;
  }

  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
    if (keyboardResult == nullptr) return;

    const std::time_t now = std::time(nullptr);
    const uint64_t createdAt = now > 0 ? static_cast<uint64_t>(now) : 0U;
    const player::PlayerServiceResult registration =
        player::runtime().registerGuest(pendingName_.data(), keyboardResult->text.c_str(), createdAt);
    setMessage(registrationMessage(registration));
    if (registration == player::PlayerServiceResult::Ok) {
      pendingName_.fill('\0');
      refreshPlayers();
    }
    requestUpdate();
  });
}

void PlayerActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (view_ == View::Callsign) {
      view_ = View::Hub;
      setMessage("");
      requestUpdate();
    } else {
      shelf::leave(renderer, mappedInput);
    }
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);

  if (view_ == View::Callsign) {
    if (event.action == playerui::ActionLeavePlayer) {
      view_ = View::Hub;
      setMessage("");
      requestUpdate();
      return;
    }
    if (event.action == playerui::ActionStepSlot && player::runtime().ready() &&
        player::runtime().guest().active()) {
      auto& guest = player::runtime().guest();
      guest.callsign = player::nextWord(guest.callsign, event.value);
      requestUpdate();
    }
    return;
  }

  if (event.action == playerhubui::ActionLoginPlayer) {
    if (event.value >= 0) startLogin(static_cast<size_t>(event.value));
    return;
  }
  if (event.action == playerhubui::ActionUseGuest) {
    const player::PlayerServiceResult result = player::runtime().useGuest();
    setMessage(result == player::PlayerServiceResult::Ok ? "GUEST SELECTED" : "GUEST UNAVAILABLE");
    requestUpdate();
    return;
  }
  if (event.action == playerhubui::ActionEditCallsign) {
    if (player::runtime().ready() && player::runtime().guest().active() && !player::runtime().hasActivePlayer()) {
      view_ = View::Callsign;
      setMessage("");
      requestUpdate();
    }
    return;
  }
  if (event.action == playerhubui::ActionRegisterGuest) {
    startRegistrationName();
  }
}

void PlayerActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);

  if (view_ == View::Callsign && player::runtime().ready() && player::runtime().guest().active()) {
    const player::GuestSession& guest = player::runtime().guest();
    char callsign[player::kMaxNameLength + 1]{};
    player::compose(callsign, sizeof(callsign), guest.callsign);

    playerui::PlayerModel model;
    model.name = callsign;
    for (int slot = 0; slot < player::kSlotCount; ++slot) {
      model.words[slot] = player::word(slot, guest.callsign.word[slot]);
    }
    playerui::buildPlayer(screen, model);
  } else {
    char callsign[player::kMaxNameLength + 1]{};
    const player::Player* active = player::runtime().activePlayer();
    const bool guestAvailable = player::runtime().ready() && player::runtime().guest().active();
    const bool guestSelected = active == nullptr;

    const char* currentName = "PLAYER ERROR";
    if (active != nullptr) {
      currentName = active->name;
      player::compose(callsign, sizeof(callsign), active->callsign);
    } else if (guestAvailable) {
      currentName = "GUEST";
      player::compose(callsign, sizeof(callsign), player::runtime().guest().callsign);
    }

    playerhubui::Model model;
    model.players = playerItems_.data();
    model.playerCount = static_cast<int>(playerCount_);
    model.activePlayerIndex = activePlayerIndex();
    model.currentName = currentName;
    model.currentCallsign = callsign;
    model.message = message_.data();
    model.guestAvailable = guestAvailable;
    model.guestSelected = guestSelected;
    model.guestCompletedMatches = guestAvailable ? player::runtime().guest().completedMatches : 0;
    model.canRegisterGuest = guestAvailable && guestSelected && model.guestCompletedMatches > 0;
    playerhubui::buildPlayerHub(screen, model);
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Player");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
