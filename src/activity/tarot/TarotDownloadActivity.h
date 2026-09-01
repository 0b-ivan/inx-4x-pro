#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <string>

#include "activity/ActivityWithSubactivity.h"

class TarotDownloadActivity final : public ActivityWithSubactivity {
 public:
  TarotDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void(bool)> onDone)
      : ActivityWithSubactivity("TarotDownload", renderer, mappedInput), onDone_(std::move(onDone)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;
  bool prioritizesScreenTouch() const override { return true; }
  bool preventAutoSleep() override { return state_ == State::Downloading; }

 private:
  enum class State { Prompt, Wifi, Downloading, Finished, Failed };
  std::atomic<State> state_{State::Prompt};
  std::function<void(bool)> onDone_;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> cancel_{false};
  std::atomic<int> completed_{0};
  std::atomic<int> total_{0};
  std::atomic<int> filePercent_{0};
  char currentFile_[128] = {};
  char error_[128] = {};
  unsigned long lastRender_ = 0;
  State lastRenderedState_ = State::Prompt;
  int lastRenderedCompleted_ = -1;
  int lastRenderedPercent_ = -1;

  static void taskTrampoline(void* parameter);
  void startWifi();
  void startDownload();
  void downloadTask();
  void render();
};
