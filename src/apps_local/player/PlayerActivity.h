#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "PlayerRuntime.h"

class PlayerActivity final : public Activity {
 public:
  PlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Player", renderer, mappedInput) {}
  ~PlayerActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class View : uint8_t { Hub, Callsign, Profile };

  void refreshPlayers();
  bool refreshProfile();
  void setMessage(const char* message);
  void startLogin(size_t index);
  void startRegistrationName();
  void startRegistrationPin();

  int activePlayerIndex() const;

  toybox::Interactions interactions;
  bool interactionsReady = false;
  View view_ = View::Hub;

  std::array<player::Player, player::PlayerRuntime::kPlayerListCapacity> players_{};
  std::array<freeink::ui::ListItem, player::PlayerRuntime::kPlayerListCapacity> playerItems_{};
  size_t playerCount_ = 0;

  std::array<player::GameStats, player::PlayerRuntime::kGameCount> profileStats_{};
  size_t profileStatsCount_ = 0;

  player::PlayerId pendingLoginId_{};
  std::array<char, player::kMaxPlayerNameLength + 1> pendingName_{};
  std::array<char, 64> message_{};
};
