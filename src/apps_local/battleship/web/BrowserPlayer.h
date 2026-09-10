#pragma once
#include "../../player/PlayerName.h"
#include "../BattleshipCore.h"
#include "BrowserCommands.h"

namespace bshipweb {
// Owned and called by the activity loop. The transport never sees Game.
class BrowserPlayer {
 public:
  void reset();
  bool apply(bship::Game& game, const Command& command);
  PlacementView view() const { return view_; }
  const char* name() const { return name_; }

 private:
  PlacementView view_;
  char name_[player::kMaxNameLength + 1] = {};
};
}  // namespace bshipweb
