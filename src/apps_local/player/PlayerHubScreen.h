#pragma once

#include <cstdint>

#include "../ui/ToyboxScreen.h"

namespace playerhubui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionLoginPlayer = 320,
  ActionUseGuest = 321,
  ActionEditCallsign = 322,
  ActionRegisterGuest = 323,
};

struct Model {
  const fui::ListItem* players = nullptr;
  int playerCount = 0;
  int activePlayerIndex = -1;

  const char* currentName = "GUEST";
  const char* currentCallsign = "";
  const char* message = "";

  bool guestAvailable = true;
  bool guestSelected = true;
  bool canRegisterGuest = false;
  uint16_t guestCompletedMatches = 0;
};

void buildPlayerHub(toybox::Screen& screen, const Model& model);

}  // namespace playerhubui
