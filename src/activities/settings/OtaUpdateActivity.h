#pragma once

#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"
#include "apps_local/ota/OtaReleaseCatalog.h"
#include "components/OptionPopup.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_RELEASES,
    BROWSING_RELEASES,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  static constexpr uint8_t CHANNEL_STABLE = 0;
  static constexpr uint8_t CHANNEL_PRERELEASE = 1;
  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  OtaReleaseCatalog releaseCatalog;
  uint8_t releaseChannel = CHANNEL_STABLE;
  int selectedRow = 0;
  size_t releaseTop = 0;

  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  OptionPopup confirmPopup;

  void onWifiSelectionComplete(bool success);
  void loadReleaseCatalog();
  void setReleaseChannel(uint8_t channel);
  void activateSelectedRow();
  void runUpdateInstall();
  void renderReleaseBrowser();
  void keepSelectionVisible(size_t visibleReleaseRows);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater(), releaseCatalog() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_RELEASES || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
