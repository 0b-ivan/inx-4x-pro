#pragma once

#include <memory>

#include "../../activities/Activity.h"

class PasskeyActivity final : public Activity {
 public:
  PasskeyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Passkey", renderer, mappedInput) {}
  ~PasskeyActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool usbStarted_ = false;
  bool shownReady_ = false;
  unsigned long shownPackets_ = 0;
};
