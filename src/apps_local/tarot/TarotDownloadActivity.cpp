#include "TarotDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <SDCardManager.h>
#include <cstdio>

#include "../ui/ToyboxFonts.h"
#include "network/HttpDownloader.h"

namespace {
constexpr char kManifest[] = "https://raw.githubusercontent.com/0b-ivan/inx-4x-pro/main/tarot/manifest.json";
constexpr char kBase[] = "https://raw.githubusercontent.com/0b-ivan/inx-4x-pro/main/tarot/";

const char* localizedError(const std::string& error) {
  if (error == "Manifest download failed") return tr(STR_TAROT_ERR_MANIFEST_DOWNLOAD);
  if (error == "Invalid manifest") return tr(STR_TAROT_ERR_MANIFEST_INVALID);
  if (error == "Unsafe manifest") return tr(STR_TAROT_ERR_MANIFEST_UNSAFE);
  if (error == "Asset download failed") return tr(STR_TAROT_ERR_ASSET_DOWNLOAD);
  if (error == "Asset size check failed") return tr(STR_TAROT_ERR_ASSET_SIZE);
  if (error == "Asset install failed") return tr(STR_TAROT_ERR_ASSET_INSTALL);
  if (error == "Menu artwork download failed") return tr(STR_TAROT_ERR_MENU_DOWNLOAD);
  if (error == "Menu install failed") return tr(STR_TAROT_ERR_MENU_INSTALL);
  return tr(STR_TAROT_DOWNLOAD_FAILED);
}
}

TarotDownloadActivity::~TarotDownloadActivity() { onExit(); }

void TarotDownloadActivity::onEnter() {
  state_ = State::Downloading;
  cancel_ = false;
  completed_ = 0;
  total_ = 0;
  percent_ = 0;
  requestUpdate();
  xTaskCreate(&TarotDownloadActivity::taskEntry, "TarotDl", 12288, this, 1, &task_);
}

void TarotDownloadActivity::onExit() {
  cancel_ = true;
  if (task_) {
    for (int i = 0; i < 50 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(20));
    if (task_) vTaskDelete(task_);
    task_ = nullptr;
  }
}

void TarotDownloadActivity::taskEntry(void* arg) {
  static_cast<TarotDownloadActivity*>(arg)->download();
  vTaskDelete(nullptr);
}

void TarotDownloadActivity::download() {
  std::string manifest;
  if (!HttpDownloader::fetchUrl(kManifest, manifest)) {
    error_ = "Manifest download failed"; state_ = State::Failed; task_ = nullptr; return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, manifest) || !doc["files"].is<JsonArray>()) {
    error_ = "Invalid manifest"; state_ = State::Failed; task_ = nullptr; return;
  }
  const JsonArray files = doc["files"].as<JsonArray>();
  total_ = static_cast<int>(files.size());
  SdMan.mkdir("/tarot"); SdMan.mkdir("/tarot/cards"); SdMan.mkdir("/tarot/thumbs");
  for (JsonObject entry : files) {
    if (cancel_) { state_ = State::Prompt; task_ = nullptr; return; }
    const std::string path = entry["path"] | "";
    const size_t expectedSize = entry["size"] | 0;
    if (path.empty() || path.find("..") != std::string::npos) { error_ = "Unsafe manifest"; state_ = State::Failed; task_ = nullptr; return; }
    const std::string destination = "/tarot/" + path;
    const std::string partial = destination + ".part";
    const auto result = HttpDownloader::downloadToFile(std::string(kBase) + path, partial,
      [this](size_t done, size_t total) { percent_ = total ? static_cast<int>(done * 100 / total) : 0; }, &cancel_);
    if (result != HttpDownloader::OK) {
      SdMan.remove(partial.c_str());
      if (result == HttpDownloader::ABORTED) { state_ = State::Prompt; task_ = nullptr; return; }
      error_ = "Asset download failed"; state_ = State::Failed; task_ = nullptr; return;
    }
    if (expectedSize > 0) {
      FsFile check = SdMan.open(partial.c_str(), O_READ);
      if (!check || check.size() != expectedSize) { if (check) check.close(); SdMan.remove(partial.c_str()); error_ = "Asset size check failed"; state_ = State::Failed; task_ = nullptr; return; }
      check.close();
    }
    if (SdMan.exists(destination.c_str())) SdMan.remove(destination.c_str());
    if (!SdMan.rename(partial.c_str(), destination.c_str())) { error_ = "Asset install failed"; state_ = State::Failed; task_ = nullptr; return; }
    ++completed_; percent_ = 0;
  }
  // The X4 Pro menu artwork is intentionally outside the upstream manifest.
  if (!cancel_) {
    ++total_;
    const std::string partial = "/tarot/menu.png.part";
    const auto result = HttpDownloader::downloadToFile(std::string(kBase) + "menu.png", partial,
      [this](size_t done, size_t total) { percent_ = total ? static_cast<int>(done * 100 / total) : 0; }, &cancel_);
    if (result != HttpDownloader::OK) {
      SdMan.remove(partial.c_str());
      if (result == HttpDownloader::ABORTED) { state_ = State::Prompt; task_ = nullptr; return; }
      error_ = "Menu artwork download failed"; state_ = State::Failed; task_ = nullptr; return;
    }
    if (SdMan.exists("/tarot/menu.png")) SdMan.remove("/tarot/menu.png");
    if (!SdMan.rename(partial.c_str(), "/tarot/menu.png")) { error_ = "Menu install failed"; state_ = State::Failed; task_ = nullptr; return; }
    ++completed_;
  }
  FsFile marker;
  if (SdMan.openFileForWrite("TAROT", "/tarot/.menu-v3", marker)) { marker.print("3"); marker.close(); }
  state_ = State::Finished;
  task_ = nullptr;
}

void TarotDownloadActivity::loop() {
  const State state = state_.load();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == State::Downloading) cancel_ = true;
    else finish();
    return;
  }
  if (state == State::Finished || state == State::Failed) {
    int tapX = 0, tapY = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tapX, tapY)) {
      if (state == State::Finished) setResult(ActivityResult{});
      finish();
      return;
    }
  }
  requestUpdate();
}

void TarotDownloadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const State state = state_.load();
  if (state == State::Downloading) {
    char line[64];
    std::snprintf(line, sizeof(line), tr(STR_TAROT_DOWNLOADING_FORMAT), completed_.load(), total_.load(),
                  percent_.load());
    renderer.drawCenteredText(toybox::kDisplayFontId, renderer.getScreenHeight() / 2 - 25, line, true);
    renderer.drawCenteredText(toybox::kUiFontId, renderer.getScreenHeight() / 2 + 30, tr(STR_TAROT_BACK_CANCEL), true);
  } else if (state == State::Finished) {
    renderer.drawCenteredText(toybox::kDisplayFontId, renderer.getScreenHeight() / 2 - 25, tr(STR_TAROT_READY), true);
    renderer.drawCenteredText(toybox::kUiFontId, renderer.getScreenHeight() / 2 + 30, tr(STR_TAROT_SELECT_CONTINUE), true);
  } else {
    renderer.drawCenteredText(toybox::kDisplayFontId, renderer.getScreenHeight() / 2 - 25,
                              error_.empty() ? tr(STR_TAROT_DOWNLOAD_FAILED) : localizedError(error_), true);
    renderer.drawCenteredText(toybox::kUiFontId, renderer.getScreenHeight() / 2 + 30, tr(STR_TAROT_SELECT_CLOSE), true);
  }
  renderer.displayBuffer();
}
