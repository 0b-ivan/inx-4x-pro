#pragma once
#include <memory>
#include <Memory.h>
#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class PasswordManagerActivity final : public Activity {
 public:
  explicit PasswordManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PasswordManager", renderer, mappedInput) {}

  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<PasswordManagerActivity>(r, m);
  }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  int getItemCount() const;
};
