#pragma once

#include <functional>

#include "activity/Activity.h"

class CalendarActivity final : public Activity {
 public:
  CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : Activity("Calendar", renderer, mappedInput), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;

 private:
  std::function<void()> onBack_;

  void render();
};
