#pragma once
#include <memory>
#include <Memory.h>
#include <cstdint>

#include "../../activities/Activity.h"

class OtpGeneratorActivity final : public Activity {
 public:
  explicit OtpGeneratorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtpGenerator", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<OtpGeneratorActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int BYTES_PER_PAGE = 100;
  static constexpr int COLS = 10;
  static constexpr int ROWS = 10;

  uint8_t pageData[BYTES_PER_PAGE] = {};
  int pageNumber = 0;

  void generatePage();
};
