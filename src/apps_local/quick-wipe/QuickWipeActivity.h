#pragma once
#include <memory>
#include <Memory.h>
#include <string>
#include "../../activities/Activity.h"

class QuickWipeActivity final : public Activity {
 public:
  explicit QuickWipeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QuickWipe", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<QuickWipeActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  static int performWipe();

 private:
  enum State { CONFIRM, WIPING, DONE, VERIFIED };
  State state = CONFIRM;
  unsigned long confirmStart = 0;
  bool holdingConfirm = false;
  int filesDeleted = 0;
  int bytesWiped = 0;

  void startWipe();
  void verifyWipe();

  void renderConfirm() const;
  void renderWiping() const;
  void renderDone() const;
  void renderVerified() const;

  static int wipeDirectory(const char* path);
};
