#pragma once
#include <memory>
#include <Memory.h>
#include <string>

#include "../../activities/Activity.h"

class QrGeneratorActivity final : public Activity {
 public:
  explicit QrGeneratorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QrGenerator", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<QrGeneratorActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { TEXT_INPUT, QR_DISPLAY };

  State state = TEXT_INPUT;
  std::string textPayload;
};
