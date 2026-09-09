#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

// Fork-local parser for GitHub's /releases array. It deliberately keeps a
// small fixed catalog instead of buffering the full JSON response: OTA runs
// while TLS is live and must not add a second large heap allocation beside it.
class OtaReleaseCatalog {
 public:
  static constexpr size_t MAX_RELEASES = 12;

  struct Entry {
    char tag[32]{};
    char firmwareUrl[512]{};
    size_t firmwareSize = 0;
    bool prerelease = false;
  };

  OtaReleaseCatalog();
  OtaReleaseCatalog(const OtaReleaseCatalog&) = delete;
  OtaReleaseCatalog& operator=(const OtaReleaseCatalog&) = delete;

  void setFirmwareAssetName(const char* name);
  // Stable can reject prereleases while the response is still streaming so a
  // run of fresh alphas cannot consume the bounded catalog before older stable
  // versions are encountered.
  void setIncludePrerelease(bool include) { includePrerelease = include; }
  void reset();
  void feed(const char* data, size_t len);

  // Remove unsupported entries, apply the selected release channel again as a
  // defensive filter, and sort newest-first using semantic-version precedence.
  void prepare(bool includePrerelease);

  size_t count() const { return entryCount; }
  const Entry* at(size_t index) const { return index < entryCount ? &entries[index] : nullptr; }

  static bool isSemverTag(const char* tag);
  // >0 when lhs is newer, <0 when rhs is newer, 0 when equal.
  static int compareTags(const char* lhs, const char* rhs);

 private:
  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    PRERELEASE,
    DRAFT,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void beginRelease();
  void beginAsset();
  void commitAsset();
  void commitRelease();

  StreamingJsonParser parser;
  Entry entries[MAX_RELEASES]{};
  size_t entryCount = 0;
  bool includePrerelease = true;

  uint8_t depth = 0;
  uint8_t releaseDepth = 0;
  uint8_t assetsDepth = 0;
  uint8_t assetDepth = 0;
  LastKey lastKey = LastKey::NONE;

  char currentTag[32]{};
  bool currentTagFound = false;
  bool currentPrerelease = false;
  bool currentDraft = false;
  bool currentFirmwareFound = false;
  char currentFirmwareUrl[512]{};
  size_t currentFirmwareSize = 0;

  char currentAssetName[32]{};
  char currentAssetUrl[512]{};
  size_t currentAssetSize = 0;
  char firmwareAssetName[32]{};
};
