#pragma once
#include <memory>
#include <Memory.h>

#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class ScreenDecoyActivity final : public Activity {
 public:
  explicit ScreenDecoyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ScreenDecoy", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<ScreenDecoyActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum DecoyType { FAKE_SHUTDOWN, FAKE_ERROR, FAKE_READING, BLANK, DECOY_COUNT };
  DecoyType selectedType = FAKE_SHUTDOWN;
  bool previewMode = true;  // true = selecting, false = about to activate

  ButtonNavigator buttonNavigator;

  void activateDecoy();
  void renderSelection() const;
  void renderDecoyPreview() const;

  // Decoy screen renderers (render directly, then sleep)
  void renderFakeShutdown() const;
  void renderFakeError() const;
  void renderFakeReading() const;
  void renderBlank() const;

  static const char* decoyName(DecoyType type);
};
