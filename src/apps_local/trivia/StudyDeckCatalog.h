#pragma once

// Fixed-capacity study-deck catalogue. No network and no resident question
// index. A filename is a deck identity, not an arbitrary SD path supplied by
// user input. Sanitise it before constructing a path.

#include <cstdint>

namespace trivia {

inline constexpr uint8_t kStudyMaxDecks = 12;
inline constexpr uint8_t kStudyDecksPerPage = 6;
inline constexpr uint8_t kStudySlugMax = 30;
inline constexpr const char* kStudyDeckFolder = "/trivia/decks";
inline constexpr const char* kAwsStudyDeckFolder = "/trivia/aws";

struct StudyDeckEntry {
  char slug[kStudySlugMax + 1] = {};
  bool aws = false;
};

class StudyDeckCatalog {
 public:
  void clear() { count_ = 0; }
  // Bare basename returned by SD listing, never a full or relative path.
  // Accept only <slug>.dat, where slug contains [a-zA-Z0-9_-].
  bool addFile(const char* filename);
  bool addAwsFile(const char* filename);
  // Lines in public GitHub catalog: AWS|CERTNAME or DECK|CERTNAME.
  // Only a validated slug is ever permitted to become a downloaded path.
  bool addManifestLine(const char* line);
  uint8_t count() const { return count_; }
  const StudyDeckEntry* at(uint8_t index) const {
    return index < count_ ? &entries_[index] : nullptr;
  }
  int find(const char* slug) const;
  // Generate pack, progress, identity, preference or topics path for entry.
  bool path(uint8_t index, const char* extension, char* out, uint32_t capacity) const;

 private:
  bool add(const char* slug, bool aws);
  StudyDeckEntry entries_[kStudyMaxDecks] = {};
  uint8_t count_ = 0;
};

} // namespace trivia
