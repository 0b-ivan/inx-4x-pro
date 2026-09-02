#include "TarotDeck.h"

#include <algorithm>
#include <esp_random.h>

TarotDeck::TarotDeck() { shuffle(); }

void TarotDeck::shuffle() {
  sequence_.resize(kSize);
  for (int i = 0; i < kSize; ++i) sequence_[static_cast<size_t>(i)] = static_cast<int8_t>(i);
  for (int i = kSize - 1; i > 0; --i) {
    const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
    std::swap(sequence_[static_cast<size_t>(i)], sequence_[static_cast<size_t>(j)]);
  }
  history_.clear();
  index_ = -1;
}

bool TarotDeck::hasNext() const { return index_ + 1 < static_cast<int>(sequence_.size()); }

int TarotDeck::drawNext() {
  if (!hasNext()) shuffle();
  ++index_;
  const int card = sequence_[static_cast<size_t>(index_)];
  history_.insert(history_.begin(), static_cast<int8_t>(card));
  return card;
}
