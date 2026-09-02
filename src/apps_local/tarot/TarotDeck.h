#pragma once

#include <cstdint>
#include <vector>

class TarotDeck {
 public:
  static constexpr int kSize = 78;
  TarotDeck();
  void shuffle();
  int drawNext();
  bool hasNext() const;
  const std::vector<int8_t>& history() const { return history_; }

 private:
  std::vector<int8_t> sequence_;
  std::vector<int8_t> history_;
  int index_ = -1;
};
