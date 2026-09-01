#include "TarotDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdio>

#include "activity/network/WifiSelectionActivity.h"
#include "network/HttpDownloader.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiI18n.h"
#include "system/UiTheme.h"

namespace {
constexpr const char* kManifestUrl =
    "https://raw.githubusercontent.com/0b-ivan/inx-4x-pro/x4pro-port/tarot/manifest.json";
constexpr const char* kAssetBaseUrl =
    "https://raw.githubusercontent.com/0b-ivan/inx-4x-pro/x4pro-port/tarot/";
constexpr const char* kMenuRelativePath = "menu.png";
constexpr size_t kMenuSize = 6734;
constexpr const char* kMenuSha256 = "9e248f8f91110c1e3545d55f2f44a86e333a61df200a13dd63ec9d74f86a5d18";
constexpr const char* kMenuVersionMarker = "/tarot/.menu-v2";

bool sha256File(const std::string& path, std::string& hex) {
  FsFile file = SdMan.open(path.c_str(), O_READ);
  if (!file) return false;
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts(&context, 0);
  uint8_t buffer[4096];
  while (file.available()) {
    const int count = file.read(buffer, sizeof(buffer));
    if (count <= 0) {
      file.close();
      mbedtls_sha256_free(&context);
      return false;
    }
    mbedtls_sha256_update(&context, buffer, static_cast<size_t>(count));
  }
  file.close();
  uint8_t digest[32];
  mbedtls_sha256_finish(&context, digest);
  mbedtls_sha256_free(&context);
  char output[65];
  for (int i = 0; i < 32; ++i) std::snprintf(output + i * 2, 3, "%02x", digest[i]);
  output[64] = '\0';
  hex = output;
  return true;
}

bool validFile(const std::string& path, const size_t expectedSize, const std::string& expectedHash) {
  FsFile file = SdMan.open(path.c_str(), O_READ);
  if (!file) return false;
  const size_t actualSize = file.size();
  file.close();
  if (actualSize != expectedSize) return false;
  std::string actualHash;
  return sha256File(path, actualHash) && actualHash == expectedHash;
}
}  // namespace

void TarotDownloadActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Prompt;
  render();
}

void TarotDownloadActivity::onExit() {
  cancel_ = true;
  if (task_) {
    for (int i = 0; i < 50 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(20));
    if (task_) vTaskDelete(task_);
    task_ = nullptr;
  }
  ActivityWithSubactivity::onExit();
}

void TarotDownloadActivity::startWifi() {
  state_ = State::Wifi;
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput, [this](const bool connected) {
    exitActivity();
    if (!connected) {
      state_ = State::Prompt;
      render();
      return;
    }
    startDownload();
  }));
}

void TarotDownloadActivity::startDownload() {
  state_ = State::Downloading;
  cancel_ = false;
  completed_ = 0;
  total_ = 0;
  filePercent_ = 0;
  error_[0] = '\0';
  render();
  xTaskCreate(&TarotDownloadActivity::taskTrampoline, "TarotAssets", 10240, this, 1, &task_);
}

void TarotDownloadActivity::taskTrampoline(void* parameter) {
  static_cast<TarotDownloadActivity*>(parameter)->downloadTask();
}

void TarotDownloadActivity::downloadTask() {
  std::string manifest;
  if (!HttpDownloader::fetchUrl(kManifestUrl, manifest, "", "")) {
    std::snprintf(error_, sizeof(error_), "Manifest could not be downloaded");
    state_ = State::Failed;
    task_ = nullptr;
    vTaskDelete(nullptr);
  }
  JsonDocument document;
  if (deserializeJson(document, manifest) || !document["files"].is<JsonArray>()) {
    std::snprintf(error_, sizeof(error_), "Invalid manifest");
    state_ = State::Failed;
    task_ = nullptr;
    vTaskDelete(nullptr);
  }
  const JsonArray files = document["files"].as<JsonArray>();
  // menu.png intentionally lives outside the original DogeReader manifest: it
  // is the X4 Pro-specific text-free, pure black/white menu artwork.
  total_ = files.size() + 1;
  SdMan.mkdir("/tarot");
  SdMan.mkdir("/tarot/cards");
  SdMan.mkdir("/tarot/thumbs");
  for (JsonObject entry : files) {
    if (cancel_) break;
    const std::string relative = entry["path"] | "";
    const std::string hash = entry["sha256"] | "";
    const size_t size = entry["size"] | 0;
    if (relative.empty() || relative.find("..") != std::string::npos || hash.size() != 64 || size == 0) {
      std::snprintf(error_, sizeof(error_), "Unsafe manifest entry");
      state_ = State::Failed;
      task_ = nullptr;
      vTaskDelete(nullptr);
    }
    std::snprintf(currentFile_, sizeof(currentFile_), "%s", relative.c_str());
    filePercent_ = 0;
    const std::string destination = "/tarot/" + relative;
    if (!validFile(destination, size, hash)) {
      const std::string partial = destination + ".part";
      const std::string url = std::string(kAssetBaseUrl) + relative;
      const auto result = HttpDownloader::downloadToFile(
          url, partial, "", "", [this](const size_t done, const size_t length) {
            if (length) filePercent_ = static_cast<int>(done * 100 / length);
          });
      if (result != HttpDownloader::OK || !validFile(partial, size, hash)) {
        SdMan.remove(partial.c_str());
        std::snprintf(error_, sizeof(error_), "Download or SHA-256 check failed");
        state_ = State::Failed;
        task_ = nullptr;
        vTaskDelete(nullptr);
      }
      if (SdMan.exists(destination.c_str())) SdMan.remove(destination.c_str());
      if (!SdMan.rename(partial.c_str(), destination.c_str())) {
        std::snprintf(error_, sizeof(error_), "Could not install downloaded file");
        state_ = State::Failed;
        task_ = nullptr;
        vTaskDelete(nullptr);
      }
    }
    ++completed_;
  }

  if (!cancel_) {
    std::snprintf(currentFile_, sizeof(currentFile_), "%s", kMenuRelativePath);
    filePercent_ = 0;
    const std::string destination = "/tarot/menu.png";
    if (!validFile(destination, kMenuSize, kMenuSha256)) {
      const std::string partial = destination + ".part";
      const std::string url = std::string(kAssetBaseUrl) + kMenuRelativePath;
      const auto result = HttpDownloader::downloadToFile(
          url, partial, "", "", [this](const size_t done, const size_t length) {
            if (length) filePercent_ = static_cast<int>(done * 100 / length);
          });
      if (result != HttpDownloader::OK || !validFile(partial, kMenuSize, kMenuSha256)) {
        SdMan.remove(partial.c_str());
        std::snprintf(error_, sizeof(error_), "Download or SHA-256 check failed");
        state_ = State::Failed;
        task_ = nullptr;
        vTaskDelete(nullptr);
      }
      if (SdMan.exists(destination.c_str())) SdMan.remove(destination.c_str());
      if (!SdMan.rename(partial.c_str(), destination.c_str())) {
        std::snprintf(error_, sizeof(error_), "Could not install downloaded file");
        state_ = State::Failed;
        task_ = nullptr;
        vTaskDelete(nullptr);
      }
    }
    ++completed_;

    FsFile menuMarker;
    if (!SdMan.openFileForWrite("TAROT", kMenuVersionMarker, menuMarker)) {
      std::snprintf(error_, sizeof(error_), "Could not install tarot menu marker");
      state_ = State::Failed;
      task_ = nullptr;
      vTaskDelete(nullptr);
    }
    menuMarker.print("2");
    menuMarker.close();
  }

  if (!cancel_) {
    FsFile marker;
    if (SdMan.openFileForWrite("TAROT", "/tarot/.installed", marker)) {
      marker.print(document["version"] | 1);
      marker.close();
    }
    state_ = State::Finished;
  } else {
    state_ = State::Prompt;
  }
  task_ = nullptr;
  vTaskDelete(nullptr);
}

void TarotDownloadActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }
  const State state = state_.load();
  if (state != lastRenderedState_) render();
  if (state == State::Downloading) {
    const int done = completed_.load();
    const int percent = filePercent_.load();
    if ((done != lastRenderedCompleted_ || percent / 20 != lastRenderedPercent_ / 20) && millis() - lastRender_ > 1200) {
      render();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) cancel_ = true;
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onDone_(state_ == State::Finished);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (state_ == State::Prompt || state_ == State::Failed) startWifi();
    else if (state_ == State::Finished) onDone_(true);
  }
}

bool TarotDownloadActivity::handleTouchTap(const int x, const int y) {
  if (subActivity) return subActivity->handleTouchTap(x, y);
  if (x < 0 || x >= renderer.getScreenWidth() || y < 0 || y >= renderer.getScreenHeight()) return false;
  const State state = state_.load();
  if (state == State::Prompt || state == State::Failed) {
    startWifi();
    return true;
  }
  if (state == State::Finished) {
    onDone_(true);
    return true;
  }
  return state == State::Downloading;
}

void TarotDownloadActivity::render() {
  lastRender_ = millis();
  lastRenderedState_ = state_.load();
  lastRenderedCompleted_ = completed_;
  lastRenderedPercent_ = filePercent_;
  renderer.clearScreen();
  const int h = renderer.getScreenHeight();
  INX_THEME.drawPageHeader(renderer, uiTr("Tarot images"));
  if (state_ == State::Prompt) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, h / 2 - 70, uiTr("Download 78 tarot cards?"), true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, h / 2 - 20, uiTr("About 14 MB will be stored on the SD card."), true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, h / 2 + 18, uiTr("Existing verified files are kept."), true);
  } else if (state_ == State::Downloading) {
    char status[64];
    std::snprintf(status, sizeof(status), "%d / %d %s", completed_.load(), total_.load(), uiTr("files"));
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, h / 2 - 70, uiTr("Downloading..."), true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, h / 2 - 20, status, true);
    const std::string clipped = renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, currentFile_,
                                                       renderer.getScreenWidth() - 50);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, h / 2 + 18, clipped.c_str(), true);
    const int barX = 50, barY = h / 2 + 58, barW = renderer.getScreenWidth() - 100;
    renderer.rectangle.render(barX, barY, barW, 10, true);
    const int overall = total_ ? std::min(100, completed_.load() * 100 / total_.load()) : 0;
    renderer.rectangle.fill(barX + 2, barY + 2, (barW - 4) * overall / 100, 6, true);
  } else if (state_ == State::Finished) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, h / 2 - 30, uiTr("Tarot images installed"), true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, h / 2 + 15, uiTr("All files passed SHA-256 verification."), true);
  } else if (state_ == State::Failed) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, h / 2 - 50, uiTr("Download failed"), true,
                           EpdFontFamily::BOLD);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, h / 2, uiTr(error_), true);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_8_FONT_ID, h / 2 + 35, uiTr("Retry resumes verified files."), true);
  }
  const auto labels = mappedInput.mapLabels(uiTr("Back"), uiTr("Select"), "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
