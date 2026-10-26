#include "OtaReleaseCatalog.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  if (dstSize == 0) return;
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

struct ParsedVersion {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  const char* prerelease = nullptr;
};

bool parseNumber(const char*& p, uint32_t& out) {
  if (*p < '0' || *p > '9') return false;
  uint32_t value = 0;
  do {
    value = value * 10U + static_cast<uint32_t>(*p - '0');
    ++p;
  } while (*p >= '0' && *p <= '9');
  out = value;
  return true;
}

bool parseVersion(const char* tag, ParsedVersion& out) {
  if (tag == nullptr || *tag == '\0') return false;
  const char* p = tag;
  if (*p == 'v' || *p == 'V') ++p;

  if (!parseNumber(p, out.major) || *p++ != '.' || !parseNumber(p, out.minor) || *p++ != '.' ||
      !parseNumber(p, out.patch)) {
    return false;
  }

  if (*p == '\0' || *p == '+') {
    out.prerelease = nullptr;
    return true;
  }
  if (*p != '-' || p[1] == '\0' || p[1] == '+') return false;
  out.prerelease = p + 1;
  return true;
}

bool identifierNumeric(const char* begin, const char* end) {
  if (begin == end) return false;
  for (const char* p = begin; p != end; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

uint32_t identifierNumber(const char* begin, const char* end) {
  uint32_t value = 0;
  for (const char* p = begin; p != end; ++p) value = value * 10U + static_cast<uint32_t>(*p - '0');
  return value;
}

int compareIdentifier(const char* lhsBegin, const char* lhsEnd, const char* rhsBegin, const char* rhsEnd) {
  const bool lhsNumeric = identifierNumeric(lhsBegin, lhsEnd);
  const bool rhsNumeric = identifierNumeric(rhsBegin, rhsEnd);
  if (lhsNumeric && rhsNumeric) {
    const uint32_t lhs = identifierNumber(lhsBegin, lhsEnd);
    const uint32_t rhs = identifierNumber(rhsBegin, rhsEnd);
    return lhs == rhs ? 0 : (lhs > rhs ? 1 : -1);
  }
  if (lhsNumeric != rhsNumeric) return lhsNumeric ? -1 : 1;

  const size_t lhsLen = static_cast<size_t>(lhsEnd - lhsBegin);
  const size_t rhsLen = static_cast<size_t>(rhsEnd - rhsBegin);
  const size_t common = lhsLen < rhsLen ? lhsLen : rhsLen;
  const int cmp = memcmp(lhsBegin, rhsBegin, common);
  if (cmp != 0) return cmp > 0 ? 1 : -1;
  return lhsLen == rhsLen ? 0 : (lhsLen > rhsLen ? 1 : -1);
}

int comparePrerelease(const char* lhs, const char* rhs) {
  if (lhs == nullptr && rhs == nullptr) return 0;
  if (lhs == nullptr) return 1;
  if (rhs == nullptr) return -1;

  const char* lp = lhs;
  const char* rp = rhs;
  while (true) {
    const char* le = lp;
    while (*le != '\0' && *le != '.' && *le != '+') ++le;
    const char* re = rp;
    while (*re != '\0' && *re != '.' && *re != '+') ++re;

    const int cmp = compareIdentifier(lp, le, rp, re);
    if (cmp != 0) return cmp;

    const bool lhsDone = *le == '\0' || *le == '+';
    const bool rhsDone = *re == '\0' || *re == '+';
    if (lhsDone || rhsDone) {
      if (lhsDone && rhsDone) return 0;
      return lhsDone ? -1 : 1;
    }
    lp = le + 1;
    rp = re + 1;
  }
}

}  // namespace

OtaReleaseCatalog::OtaReleaseCatalog()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}) {
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), "firmware.bin", sizeof("firmware.bin") - 1);
  reset();
}

void OtaReleaseCatalog::setFirmwareAssetName(const char* name) {
  if (name == nullptr) return;
  safeCopy(firmwareAssetName, sizeof(firmwareAssetName), name, strlen(name));
}

void OtaReleaseCatalog::reset() {
  parser.reset();
  entryCount = 0;
  depth = 0;
  releaseDepth = 0;
  assetsDepth = 0;
  assetDepth = 0;
  lastKey = LastKey::NONE;
  beginRelease();
  releaseDepth = 0;
}

void OtaReleaseCatalog::feed(const char* data, const size_t len) { parser.feed(data, len); }

bool OtaReleaseCatalog::isSemverTag(const char* tag) {
  ParsedVersion parsed;
  return parseVersion(tag, parsed);
}

int OtaReleaseCatalog::compareTags(const char* lhs, const char* rhs) {
  ParsedVersion a;
  ParsedVersion b;
  const bool aValid = parseVersion(lhs, a);
  const bool bValid = parseVersion(rhs, b);
  if (aValid != bValid) return aValid ? 1 : -1;
  if (!aValid) {
    const int cmp = strcmp(lhs ? lhs : "", rhs ? rhs : "");
    return cmp == 0 ? 0 : (cmp > 0 ? 1 : -1);
  }

  if (a.major != b.major) return a.major > b.major ? 1 : -1;
  if (a.minor != b.minor) return a.minor > b.minor ? 1 : -1;
  if (a.patch != b.patch) return a.patch > b.patch ? 1 : -1;
  return comparePrerelease(a.prerelease, b.prerelease);
}

void OtaReleaseCatalog::prepare(const bool includePrereleaseValue) {
  size_t write = 0;
  for (size_t read = 0; read < entryCount; ++read) {
    if (!isSemverTag(entries[read].tag)) continue;
    if (!includePrereleaseValue && entries[read].prerelease) continue;
    if (write != read) entries[write] = entries[read];
    ++write;
  }
  entryCount = write;

  for (size_t i = 1; i < entryCount; ++i) {
    Entry current = entries[i];
    size_t j = i;
    while (j > 0 && compareTags(current.tag, entries[j - 1].tag) > 0) {
      entries[j] = entries[j - 1];
      --j;
    }
    entries[j] = current;
  }
}

void OtaReleaseCatalog::beginRelease() {
  currentTag[0] = '\0';
  currentTagFound = false;
  currentPrerelease = false;
  currentDraft = false;
  currentFirmwareFound = false;
  currentFirmwareUrl[0] = '\0';
  currentFirmwareSize = 0;
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void OtaReleaseCatalog::beginAsset() {
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void OtaReleaseCatalog::commitAsset() {
  if (strcmp(currentAssetName, firmwareAssetName) == 0 && currentAssetUrl[0] != '\0') {
    safeCopy(currentFirmwareUrl, sizeof(currentFirmwareUrl), currentAssetUrl, strlen(currentAssetUrl));
    currentFirmwareSize = currentAssetSize;
    currentFirmwareFound = true;
  }
  beginAsset();
}

void OtaReleaseCatalog::commitRelease() {
  const bool channelAllowed = includePrerelease || !currentPrerelease;
  if (entryCount < MAX_RELEASES && channelAllowed && !currentDraft && currentTagFound && currentFirmwareFound) {
    Entry& entry = entries[entryCount++];
    safeCopy(entry.tag, sizeof(entry.tag), currentTag, strlen(currentTag));
    safeCopy(entry.firmwareUrl, sizeof(entry.firmwareUrl), currentFirmwareUrl, strlen(currentFirmwareUrl));
    entry.firmwareSize = currentFirmwareSize;
    entry.prerelease = currentPrerelease;
  }
  beginRelease();
}

void OtaReleaseCatalog::sOnKey(void* ctx, const char* key, const size_t len) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  self->lastKey = LastKey::NONE;

  if (self->releaseDepth != 0 && self->depth == self->releaseDepth) {
    if (len == 8 && memcmp(key, "tag_name", 8) == 0)
      self->lastKey = LastKey::TAG_NAME;
    else if (len == 10 && memcmp(key, "prerelease", 10) == 0)
      self->lastKey = LastKey::PRERELEASE;
    else if (len == 5 && memcmp(key, "draft", 5) == 0)
      self->lastKey = LastKey::DRAFT;
    else if (len == 6 && memcmp(key, "assets", 6) == 0)
      self->lastKey = LastKey::ASSETS;
  } else if (self->assetDepth != 0 && self->depth == self->assetDepth) {
    if (len == 4 && memcmp(key, "name", 4) == 0)
      self->lastKey = LastKey::ASSET_NAME;
    else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
      self->lastKey = LastKey::ASSET_URL;
    else if (len == 4 && memcmp(key, "size", 4) == 0)
      self->lastKey = LastKey::ASSET_SIZE;
  }
}

void OtaReleaseCatalog::sOnString(void* ctx, const char* value, const size_t len) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  if (self->lastKey == LastKey::TAG_NAME && self->depth == self->releaseDepth) {
    safeCopy(self->currentTag, sizeof(self->currentTag), value, len);
    self->currentTagFound = true;
  } else if (self->lastKey == LastKey::ASSET_NAME && self->depth == self->assetDepth) {
    safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
  } else if (self->lastKey == LastKey::ASSET_URL && self->depth == self->assetDepth) {
    safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
  }
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  if (self->lastKey == LastKey::ASSET_SIZE && self->depth == self->assetDepth) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnBool(void* ctx, const bool value) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  if (self->depth == self->releaseDepth) {
    if (self->lastKey == LastKey::PRERELEASE) self->currentPrerelease = value;
    if (self->lastKey == LastKey::DRAFT) self->currentDraft = value;
  }
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnNull(void* ctx) { static_cast<OtaReleaseCatalog*>(ctx)->lastKey = LastKey::NONE; }

void OtaReleaseCatalog::sOnObjectStart(void* ctx) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  ++self->depth;
  if (self->depth == 2 && self->releaseDepth == 0) {
    self->releaseDepth = self->depth;
    self->beginRelease();
  } else if (self->assetsDepth != 0 && self->depth == self->assetsDepth + 1 && self->assetDepth == 0) {
    self->assetDepth = self->depth;
    self->beginAsset();
  }
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  if (self->assetDepth != 0 && self->depth == self->assetDepth) {
    self->commitAsset();
    self->assetDepth = 0;
  } else if (self->releaseDepth != 0 && self->depth == self->releaseDepth) {
    self->commitRelease();
    self->releaseDepth = 0;
    self->assetsDepth = 0;
  }
  if (self->depth > 0) --self->depth;
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnArrayStart(void* ctx) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  ++self->depth;
  if (self->releaseDepth != 0 && self->depth == self->releaseDepth + 1 && self->lastKey == LastKey::ASSETS) {
    self->assetsDepth = self->depth;
  }
  self->lastKey = LastKey::NONE;
}

void OtaReleaseCatalog::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<OtaReleaseCatalog*>(ctx);
  if (self->assetsDepth != 0 && self->depth == self->assetsDepth) self->assetsDepth = 0;
  if (self->depth > 0) --self->depth;
  self->lastKey = LastKey::NONE;
}
