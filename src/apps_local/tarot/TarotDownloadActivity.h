#pragma once
#include "../../activities/Activity.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

class TarotDownloadActivity final : public Activity {
 public:
  TarotDownloadActivity(GfxRenderer& r, MappedInputManager& i) : Activity("TarotDownload", r, i) {}
  ~TarotDownloadActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
 private:
  enum class State { Prompt, Downloading, Finished, Failed };
  static void taskEntry(void*);
  void download();
  std::atomic<State> state_{State::Prompt};
  bool cancel_ = false;
  std::atomic<int> completed_{0};
  std::atomic<int> total_{0};
  std::atomic<int> percent_{0};
  TaskHandle_t task_ = nullptr;
  std::string error_;
};
