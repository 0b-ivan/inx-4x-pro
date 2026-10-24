#pragma once
#include <memory>
#include <Memory.h>
#include <WebServer.h>
#include <HalStorage.h>
#include <string>
#include "../../activities/Activity.h"
#include "../../util/ButtonNavigator.h"

class DeadDropActivity final : public Activity {
 public:
  explicit DeadDropActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeadDrop", renderer, mappedInput) {}
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& m) {
    return makeUniqueNoThrow<DeadDropActivity>(r, m);
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == ACTIVE; }
  bool skipLoopDelay() override { return state == ACTIVE; }

 private:
  enum State { CONFIG, ACTIVE, DONE };
  State state = CONFIG;
  ButtonNavigator buttonNavigator;
  int configIndex = 0;

  // Config
  int timerMinutes = 5;
  static constexpr int TIMER_OPTIONS[] = {1, 5, 10, 30};
  static constexpr int TIMER_COUNT = 4;
  int timerIndex = 1;

  // Active state
  unsigned long startTime = 0;
  int filesReceived = 0;
  int connectedClients = 0;
  std::string ssidName;
  WebServer* server = nullptr;
  static constexpr const char* DROP_DIR = "/crossplay/drop/";

  // Upload state
  HalFile uploadFile;
  bool uploadInProgress = false;
  bool uploadComplete = false;
  int uploadHttpError = 0;
  std::string uploadPath;
  std::string uploadTempPath;

  void startDrop();
  void stopDrop();
  void setupWebServer();

  // Web handlers
  void handleRoot();
  void handleUpload();
  void handleUploadData();
  void handleFileList();
  void handleDownload();

  void renderConfig() const;
  void renderActive() const;
  void renderDone() const;

  static DeadDropActivity* activeInstance;
};
