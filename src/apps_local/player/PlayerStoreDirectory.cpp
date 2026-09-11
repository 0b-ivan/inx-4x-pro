#include "PlayerStore.h"

#include <array>
#include <cctype>
#include <cstring>

namespace player {
namespace {

int compareNamesCaseInsensitive(const char* left, const char* right) {
  while (*left != '\0' && *right != '\0') {
    const unsigned char leftChar = static_cast<unsigned char>(*left++);
    const unsigned char rightChar = static_cast<unsigned char>(*right++);
    const int foldedLeft = std::tolower(leftChar);
    const int foldedRight = std::tolower(rightChar);
    if (foldedLeft < foldedRight) return -1;
    if (foldedLeft > foldedRight) return 1;
  }
  if (*left == *right) return 0;
  return *left == '\0' ? -1 : 1;
}

int compareIds(const PlayerId& left, const PlayerId& right) {
  return std::memcmp(left.bytes.data(), right.bytes.data(), PlayerId::kSize);
}

}  // namespace

StoreResult PlayerStore::listPlayers(Player* out, const size_t capacity, size_t& count) const {
  count = 0;
  if (!open_) return StoreResult::NotOpen;
  if (capacity == 0) return StoreResult::Ok;
  if (out == nullptr) return StoreResult::InvalidArgument;

  std::array<size_t, kMaxPlayers> order{};
  for (size_t index = 0; index < playerCount_; ++index) order[index] = index;

  // The old SQLite query ordered by name COLLATE NOCASE, then creation time and
  // finally id. Keep exactly that deterministic picker order without allocating.
  for (size_t index = 1; index < playerCount_; ++index) {
    const size_t selected = order[index];
    size_t insertion = index;
    while (insertion > 0) {
      const Player& left = players_[selected].player;
      const Player& right = players_[order[insertion - 1]].player;
      const int nameOrder = compareNamesCaseInsensitive(left.name, right.name);
      const bool before = nameOrder < 0 ||
                          (nameOrder == 0 &&
                           (left.createdAt < right.createdAt ||
                            (left.createdAt == right.createdAt && compareIds(left.id, right.id) < 0)));
      if (!before) break;
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = selected;
  }

  const size_t limit = capacity < playerCount_ ? capacity : playerCount_;
  for (; count < limit; ++count) out[count] = players_[order[count]].player;
  return StoreResult::Ok;
}

}  // namespace player
