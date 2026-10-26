#pragma once

#include <memory>
#include <vector>
#include <string>
#include <Memory.h>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

// Read-only WLAN hardware/driver diagnostics. Never changes radio state.
class WifiDiagnosticsActivity final : public Activity {
 public:
  explicit WifiDiagnosticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WifiDiagnostics", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<WifiDiagnosticsActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<std::string> lines;
  int scrollOffset = 0;

  void refreshSnapshot();
  int visibleLineCount() const;
  void scrollBy(int delta);
};
