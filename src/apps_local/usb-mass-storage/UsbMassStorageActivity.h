#pragma once
#include <memory>
#include <Memory.h>
#include "../../activities/Activity.h"

class UsbMassStorageActivity final : public Activity {
 public:
  explicit UsbMassStorageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbMassStorage", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<UsbMassStorageActivity>(r, m);
  }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == ACTIVE; }

 private:
  enum State { READY, ACTIVE, EJECTED };
  State state = READY;

  void initMassStorage();
  void deinitMassStorage();
};
