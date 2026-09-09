#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "OtaReleaseCatalog.h"
#include "OtaUpdater.h"

namespace {

void feedChunked(OtaReleaseCatalog& catalog, const std::string& json, const size_t chunkSize = 7) {
  for (size_t offset = 0; offset < json.size(); offset += chunkSize) {
    const size_t len = std::min(chunkSize, json.size() - offset);
    catalog.feed(json.data() + offset, len);
  }
}

const OtaReleaseCatalog::Entry* findTag(const OtaReleaseCatalog& catalog, const char* tag) {
  for (size_t i = 0; i < catalog.count(); ++i) {
    const auto* entry = catalog.at(i);
    if (entry != nullptr && std::strcmp(entry->tag, tag) == 0) return entry;
  }
  return nullptr;
}

std::string releaseJson(const std::string& tag, const bool prerelease, const bool draft, const std::string& assetName,
                        const size_t assetSize = 1234) {
  return "{\"tag_name\":\"" + tag + "\",\"prerelease\":" + (prerelease ? "true" : "false") +
         ",\"draft\":" + (draft ? "true" : "false") +
         ",\"author\":{\"login\":\"nested-user\",\"name\":\"must-not-be-read-as-asset\"},\"assets\":["
         "{\"name\":\"notes.txt\",\"browser_download_url\":\"https://example.invalid/notes\",\"size\":9},"
         "{\"name\":\"" +
         assetName + "\",\"browser_download_url\":\"https://example.invalid/" + tag + "/" + assetName +
         "\",\"size\":" + std::to_string(assetSize) + "}]}";
}

void testSemverOrdering() {
  assert(OtaReleaseCatalog::isSemverTag("v1.14.0"));
  assert(OtaReleaseCatalog::isSemverTag("1.14.0-alpha.1"));
  assert(!OtaReleaseCatalog::isSemverTag("nightly"));
  assert(OtaReleaseCatalog::compareTags("v1.14.0", "v1.14.0-alpha.10") > 0);
  assert(OtaReleaseCatalog::compareTags("v1.14.0-alpha.10", "v1.14.0-alpha.2") > 0);
  assert(OtaReleaseCatalog::compareTags("v1.14.0-alpha.2", "v1.14.0-alpha.1") > 0);
  assert(OtaReleaseCatalog::compareTags("v1.14.0-alpha.1", "v1.13.4") > 0);
  assert(OtaReleaseCatalog::compareTags("v1.14.0+build.2", "v1.14.0+build.1") == 0);
}

void testStableChannelAndExactAsset() {
  const std::string json = "[" + releaseJson("v1.14.0-alpha.2", true, false, "firmware.bin") + "," +
                           releaseJson("v1.13.4", false, false, "firmware.bin", 4321) + "," +
                           releaseJson("v9.0.0", false, true, "firmware.bin") + "," +
                           releaseJson("v1.13.3", false, false, "firmware-x4pro.bin") + "," +
                           releaseJson("nightly", false, false, "firmware.bin") + "]";

  OtaReleaseCatalog catalog;
  catalog.setFirmwareAssetName("firmware.bin");
  catalog.setIncludePrerelease(false);
  catalog.reset();
  feedChunked(catalog, json, 3);
  catalog.prepare(false);

  assert(catalog.count() == 1);
  const auto* entry = catalog.at(0);
  assert(entry != nullptr);
  assert(std::strcmp(entry->tag, "v1.13.4") == 0);
  assert(std::strcmp(entry->firmwareUrl, "https://example.invalid/v1.13.4/firmware.bin") == 0);
  assert(entry->firmwareSize == 4321);
  assert(!entry->prerelease);
}

void testPrereleaseChannelIncludesBothAndSorts() {
  const std::string json = "[" + releaseJson("v1.13.4", false, false, "firmware.bin") + "," +
                           releaseJson("v1.14.0-alpha.2", true, false, "firmware.bin") + "," +
                           releaseJson("v1.14.0-alpha.10", true, false, "firmware.bin") + "," +
                           releaseJson("v1.14.0-alpha.1", true, false, "firmware.bin") + "," +
                           releaseJson("v1.14.0", false, false, "firmware.bin") + "]";

  OtaReleaseCatalog catalog;
  catalog.setFirmwareAssetName("firmware.bin");
  catalog.setIncludePrerelease(true);
  catalog.reset();
  feedChunked(catalog, json, 5);
  catalog.prepare(true);

  const char* expected[] = {"v1.14.0", "v1.14.0-alpha.10", "v1.14.0-alpha.2", "v1.14.0-alpha.1", "v1.13.4"};
  assert(catalog.count() == 5);
  for (size_t i = 0; i < 5; ++i) {
    assert(catalog.at(i) != nullptr);
    assert(std::strcmp(catalog.at(i)->tag, expected[i]) == 0);
  }
}

void testStableNotCrowdedOutByPrereleases() {
  std::string json = "[";
  for (int i = 0; i < 20; ++i) {
    if (i != 0) json += ',';
    json += releaseJson("v2.0.0-alpha." + std::to_string(20 - i), true, false, "firmware.bin");
  }
  json += ',' + releaseJson("v1.13.4", false, false, "firmware.bin") + ']';

  OtaReleaseCatalog catalog;
  catalog.setFirmwareAssetName("firmware.bin");
  catalog.setIncludePrerelease(false);
  catalog.reset();
  feedChunked(catalog, json, 11);
  catalog.prepare(false);

  assert(catalog.count() == 1);
  assert(findTag(catalog, "v1.13.4") != nullptr);
}

void testVersionDirectionPolicy() {
  // Automatic/latest flow: equal or older must still be rejected.
  assert(OtaUpdater::shouldRejectVersion(false, false));
  assert(!OtaUpdater::shouldRejectVersion(false, true));
  // Explicit picker: downgrade/reinstall may pass the direction gate.
  assert(!OtaUpdater::shouldRejectVersion(true, false));
  assert(!OtaUpdater::shouldRejectVersion(true, true));
}

}  // namespace

int main() {
  testSemverOrdering();
  testStableChannelAndExactAsset();
  testPrereleaseChannelIncludesBothAndSorts();
  testStableNotCrowdedOutByPrereleases();
  testVersionDirectionPolicy();
  std::cout << "OTA release catalog tests passed\n";
  return 0;
}
