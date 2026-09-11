#pragma once

#include "GameStats.h"
#include "PlayerProgression.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxScreen.h"

namespace playerprofileui {

namespace fui = freeink::ui;

struct Model {
  const char* name = "GUEST";
  const char* callsign = "";
  bool guest = true;

  player::ProgressionSnapshot progression{};
  player::GameStats battleship{};
  const char* battleshipRank = "";
  uint32_t nextBattleshipRankWins = 0;
};

void buildPlayerProfile(toybox::Screen& screen, const Model& model, const GfxRenderer& renderer);

}  // namespace playerprofileui
