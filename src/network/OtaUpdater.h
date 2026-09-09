#pragma once

#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
    TOO_LARGE_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();

  // Select a concrete release returned by the repository's release catalog.
  // This only stages metadata; installUpdate() still performs all chip, board
  // tag, image-size and esp_ota validation before changing the boot partition.
  bool selectRelease(const char* version, const char* firmwareUrl, size_t firmwareSize);

  // Existing callers keep the historical "newer only" behaviour.  The OTA
  // release picker opts into allowOlder after an explicit user selection so a
  // downgrade uses exactly the same guarded flash path as an upgrade.
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr, bool allowOlder = false);
};
