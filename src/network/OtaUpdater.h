#pragma once

#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  bool allowOlderSelection = false;
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
  // Explicit manual selections may opt into older/equal versions, but that
  // changes only the version-direction gate. installUpdate() still performs
  // all chip, board-tag, image-size and esp_ota validation.
  bool selectRelease(const char* version, const char* firmwareUrl, const size_t firmwareSize,
                     const bool allowOlder = false) {
    if (version == nullptr || version[0] == '\0' || firmwareUrl == nullptr || firmwareUrl[0] == '\0') {
      updateAvailable = false;
      allowOlderSelection = false;
      return false;
    }

    latestVersion = version;
    if (!latestVersion.empty() && (latestVersion[0] == 'v' || latestVersion[0] == 'V')) {
      latestVersion.erase(0, 1);
    }
    otaUrl = firmwareUrl;
    otaSize = firmwareSize;
    processedSize = 0;
    totalSize = otaSize;
    updateAvailable = true;
    allowOlderSelection = allowOlder;
    return true;
  }

  // Pure policy seam for host tests: explicit manual selection bypasses only
  // the version-direction rejection. All image/device validation lives later
  // in installUpdate() and is independent of this result.
  static constexpr bool shouldRejectVersion(const bool allowOlder, const bool isNewer) {
    return !allowOlder && !isNewer;
  }

  // Keep the historical two-argument API: the simulator provides this exact
  // firmware stub. Manual selection stores its direction policy above instead
  // of changing the ABI used by that simulator.
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
