#pragma once
#include "../../player/PlayerName.h"
#include "../BattleshipCore.h"
#include "BrowserCommands.h"

namespace bshipweb {
// Owned and called by the activity loop. The transport never sees Game.
class BrowserPlayer {
 public:
  void reset();
  // Inject a device-generated seed once per browser game session. Keep the
  // generator state across rematches so a predictable revision cannot reveal
  // the browser fleet. Host tests can use a fixed seed for reproducibility.
  void setRandomSeed(uint32_t seed) { randomSeed_ = seed ? seed : 0x9e3779b9u; }
  bool apply(bship::Game& game, const Command& command);
  PlacementView view() const { return view_; }
  const char* name() const { return name_; }

 private:
  PlacementView view_;
  uint32_t randomSeed_ = 0x9e3779b9u;
  char name_[player::kMaxNameLength + 1] = {};
};
}  // namespace bshipweb
