#pragma once
#include <memory>
#include <string>
#include <vector>

#include <Memory.h>
#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class CredentialViewerActivity final : public Activity {
 public:
  explicit CredentialViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CredentialViewer", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<CredentialViewerActivity>(r, m);
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { LIST_VIEW, DETAIL_VIEW, EMPTY_VIEW, CONFIRM_DELETE };

  struct Credential {
    std::string timestamp;
    std::string ssid;
    std::string username;
    std::string password;
  };

  State state = EMPTY_VIEW;
  std::vector<Credential> creds;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  void loadCredentials();
  void deleteAllCredentials();
};
