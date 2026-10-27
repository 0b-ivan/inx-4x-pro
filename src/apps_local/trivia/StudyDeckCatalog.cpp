#include "StudyDeckCatalog.h"

#include <cstdio>
#include <cstring>

namespace trivia {
namespace {

bool validSlug(const char* s) {
  if (!s || !*s) return false;
  uint32_t n = 0;
  while (*s) {
    const char c = *s++;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    if (++n > kStudySlugMax) return false;
  }
  return n > 0;
}
bool validExtension(const char* ext) {
  return ext && (!std::strcmp(ext, ".dat") || !std::strcmp(ext, ".state") ||
                 !std::strcmp(ext, ".state.id") || !std::strcmp(ext, ".prefs") ||
                 !std::strcmp(ext, ".topics"));
}
} // namespace

bool StudyDeckCatalog::add(const char* slug, const bool aws) {
  if (!validSlug(slug) || count_ >= kStudyMaxDecks || find(slug) >= 0) return false;
  // Readdir order is filesystem-dependent. Stable sorting makes the on-device
  // selection and simulator touch targets refer to the same certificate on
  // every boot, regardless of how FAT/host directories enumerate their files.
  uint8_t pos = 0;
  while (pos < count_ && std::strcmp(entries_[pos].slug, slug) < 0) ++pos;
  for (uint8_t i = count_; i > pos; --i) entries_[i] = entries_[i - 1];
  StudyDeckEntry& target = entries_[pos];
  std::snprintf(target.slug, sizeof(target.slug), "%s", slug);
  target.aws = aws;
  ++count_;
  return true;
}

// The catalog parses filenames independently of the SD implementation.
bool StudyDeckCatalog::addFile(const char* filename) {
  if (!filename) return false;
  const uint32_t len = static_cast<uint32_t>(std::strlen(filename));
  if (len <= 4 || len > kStudySlugMax + 4 ||
      std::strcmp(filename + len - 4, ".dat") != 0) return false;
  char slug[kStudySlugMax + 1] = {};
  std::memcpy(slug, filename, len - 4);
  return add(slug, false);
}
bool StudyDeckCatalog::addAwsFile(const char* filename) {
  if (!filename) return false;
  const uint32_t len = static_cast<uint32_t>(std::strlen(filename));
  if (len <= 4 || len > kStudySlugMax + 4 ||
      std::strcmp(filename + len - 4, ".dat") != 0) return false;
  char slug[kStudySlugMax + 1] = {};
  std::memcpy(slug, filename, len - 4);
  return add(slug, true);
}

bool StudyDeckCatalog::addManifestLine(const char* line) {
  if (!line) return false;
  const bool aws = std::strncmp(line, "AWS|", 4) == 0;
  const bool other = std::strncmp(line, "DECK|", 5) == 0;
  if (!aws && !other) return false;
  const char* slug = line + (aws ? 4 : 5);
  return add(slug, aws);
}

int StudyDeckCatalog::find(const char* slug) const {
  if (!slug) return -1;
  for (uint8_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].slug, slug) == 0) return i;
  }
  return -1;
}

bool StudyDeckCatalog::path(uint8_t index, const char* ext, char* out,
                            uint32_t capacity) const {
  if (index >= count_ || !out || capacity == 0 || !validExtension(ext)) return false;
  const StudyDeckEntry& deck = entries_[index];
  const int result = std::snprintf(out, capacity, "%s/%s%s",
                                   deck.aws ? kAwsStudyDeckFolder : kStudyDeckFolder,
                                   deck.slug, ext);
  return result >= 0 && static_cast<uint32_t>(result) < capacity;
}

} // namespace trivia
