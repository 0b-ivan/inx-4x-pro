#pragma once
// Cards Against Whatever: bounded, firmware-independent player history and
// non-fit voting. This file owns NO network or SD write operations.
// The X4 persists snapshots between rounds and retains a .bak on SD.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace cardparty {
namespace history {
constexpr size_t kPlayersStored = 64;
constexpr size_t kWinningLines = 20;
constexpr size_t kNonFitPairs = 2048;
constexpr size_t kSentenceBytes = 200;

struct Player {
  uint64_t id = 0;  // public label, not a secret or an authentication token
  char name[24] = {};
  uint8_t word = 0;
  uint32_t games = 0, wins = 0, rounds = 0, seen = 0;
};
struct WinningLine {
  uint64_t player = 0;
  uint32_t seen = 0;
  char sentence[kSentenceBytes + 1] = {};
};
struct Pair {
  uint32_t pack = 0;
  uint16_t prompt = 0, answer = 0;
  uint8_t count = 0;
  uint64_t voters[3] = {};
};

inline uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return ~crc;
}

class Book final {
 public:
  enum class Vote : uint8_t { Invalid, Duplicate, Recorded, Blocked, Full };
  bool upsert(uint64_t id, const char* name, uint32_t seen) {
    if (!id || !name || !*name || std::strlen(name) > 23) return false;
    Player* p = find(id);
    if (!p) {
      if (count_ < kPlayersStored)
        p = &players_[count_++];
      else {
        // Evict the least-recently seen identity, never a random entry.
        p = &players_[0];
        for (size_t i = 1; i < count_; ++i)
          if (players_[i].seen < p->seen) p = &players_[i];
      }
      *p = Player{};
      p->id = id;
      p->word = wordFor(id);
    }
    std::strncpy(p->name, name, sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = 0;
    p->seen = seen;
    return true;
  }
  const Player* player(uint64_t id) const {
    for (size_t i = 0; i < count_; ++i)
      if (players_[i].id == id) return &players_[i];
    return nullptr;
  }
  Player* find(uint64_t id) {
    for (size_t i = 0; i < count_; ++i)
      if (players_[i].id == id) return &players_[i];
    return nullptr;
  }
  static uint8_t wordFor(uint64_t id) {
    // Stable on every device and independent of a session RNG.
    id ^= id >> 33; id *= UINT64_C(0xff51afd7ed558ccd);
    id ^= id >> 33; id *= UINT64_C(0xc4ceb9fe1a85ec53);
    id ^= id >> 33;
    return static_cast<uint8_t>(id & 63);
  }
  bool played(uint64_t id) {
    Player* p = find(id);
    if (!p) return false;
    if (p->games != UINT32_MAX) ++p->games;
    return true;
  }
  bool gameWon(uint64_t id) {
    Player* p = find(id);
    if (!p) return false;
    if (p->wins != UINT32_MAX) ++p->wins;
    return true;
  }
  bool roundWon(uint64_t id, const char* sentence, uint32_t seen) {
    Player* p = find(id);
    if (!p) return false;
    if (p->rounds != UINT32_MAX) ++p->rounds;
    if (!sentence || !*sentence) return true;
    // Keep latest first, including ties. UTF-8 cannot end mid-sequence.
    for (size_t i = std::min(quoteCount_, kWinningLines - 1); i > 0; --i)
      quotes_[i] = quotes_[i - 1];
    WinningLine& quote = quotes_[0];
    quote = WinningLine{};
    quote.player = id;
    quote.seen = seen;
    size_t len = std::min(std::strlen(sentence), kSentenceBytes);
    while (len && (static_cast<unsigned char>(sentence[len]) & 0xc0u) == 0x80u) --len;
    std::memcpy(quote.sentence, sentence, len);
    quote.sentence[len] = 0;
    quoteCount_ = std::min(quoteCount_ + 1, kWinningLines);
    return true;
  }
  Vote strike(uint64_t id, uint32_t pack, uint16_t prompt, uint16_t answer) {
    if (!id || prompt == UINT16_MAX || answer == UINT16_MAX) return Vote::Invalid;
    for (Pair& entry : pairs_) {
      if (entry.pack != pack || entry.prompt != prompt || entry.answer != answer) continue;
      if (entry.count >= 3) return Vote::Blocked;
      for (uint8_t i = 0; i < entry.count; ++i)
        if (entry.voters[i] == id) return Vote::Duplicate;
      entry.voters[entry.count++] = id;
      return entry.count == 3 ? Vote::Blocked : Vote::Recorded;
    }
    if (pairs_.size() == kNonFitPairs) {
      // Avoid losing hard-won consensus. Only single-vote pairs can be evicted.
      auto victim = std::find_if(pairs_.begin(), pairs_.end(), [](const Pair& p) { return p.count == 1; });
      if (victim == pairs_.end()) return Vote::Full;
      *victim = Pair{pack, prompt, answer, 1, {id, 0, 0}};
      return Vote::Recorded;
    }
    pairs_.push_back(Pair{pack, prompt, answer, 1, {id, 0, 0}});
    return Vote::Recorded;
  }
  bool blocked(uint32_t pack, uint16_t prompt, uint16_t answer) const {
    for (const Pair& p : pairs_)
      if (p.pack == pack && p.prompt == prompt && p.answer == answer) return p.count == 3;
    return false;
  }
  size_t players() const { return count_; }
  size_t quotes() const { return quoteCount_; }
  size_t pairs() const { return pairs_.size(); }
  const Player& at(size_t i) const { return players_[i]; }
  const WinningLine& quote(size_t i) const { return quotes_[i]; }

  std::vector<uint8_t> encode() const {
    std::vector<uint8_t> out;
    out.reserve(16 + count_ * 45 + quoteCount_ * 220 + pairs_.size() * 33);
    for (char c : {'C','A','W','H'}) out.push_back(static_cast<uint8_t>(c));
    put(out, 1, 1);
    put(out, count_, 1);
    put(out, quoteCount_, 1);
    put(out, pairs_.size(), 2);
    for (size_t i = 0; i < count_; ++i) {
      const Player& p = players_[i];
      put(out, p.id, 8);
      put(out, p.word, 1);
      put(out, p.games, 4); put(out, p.wins, 4); put(out, p.rounds, 4); put(out, p.seen, 4);
      for (char c : p.name) out.push_back(static_cast<uint8_t>(c));
    }
    for (size_t i = 0; i < quoteCount_; ++i) {
      const WinningLine& q = quotes_[i];
      put(out, q.player, 8); put(out, q.seen, 4);
      for (char c : q.sentence) out.push_back(static_cast<uint8_t>(c));
    }
    for (const Pair& p : pairs_) {
      put(out, p.pack, 4); put(out, p.prompt, 2); put(out, p.answer, 2); put(out, p.count, 1);
      for (uint64_t voter : p.voters) put(out, voter, 8);
    }
    put(out, crc32(out.data(), out.size()), 4);
    return out;
  }
  bool decode(const uint8_t* bytes, size_t size) {
    if (!bytes || size < 13 || std::memcmp(bytes, "CAWH", 4) != 0 || bytes[4] != 1) return false;
    const uint32_t check = static_cast<uint32_t>(bytes[size-4]) |
      static_cast<uint32_t>(bytes[size-3]) << 8 |
      static_cast<uint32_t>(bytes[size-2]) << 16 |
      static_cast<uint32_t>(bytes[size-1]) << 24;
    if (crc32(bytes, size-4) != check) return false;
    const size_t pc = bytes[5], qc = bytes[6];
    const size_t sc = static_cast<size_t>(bytes[7]) | static_cast<size_t>(bytes[8]) << 8;
    if (pc > kPlayersStored || qc > kWinningLines || sc > kNonFitPairs ||
        size != 9 + pc * 49 + qc * 213 + sc * 33 + 4) return false;
    Book candidate;
    candidate.count_ = pc; candidate.quoteCount_ = qc;
    candidate.pairs_.reserve(sc);
    size_t pos = 9;
    for (size_t i = 0; i < pc; ++i) {
      Player& p = candidate.players_[i];
      p.id = get(bytes,pos,8); p.word = static_cast<uint8_t>(get(bytes,pos,1));
      p.games = get(bytes,pos,4); p.wins = get(bytes,pos,4); p.rounds = get(bytes,pos,4);
      p.seen = get(bytes,pos,4);
      std::memcpy(p.name,bytes+pos,24);pos+=24;
      if (!p.id || p.name[23] != 0 || p.word >= 64) return false;
      for (size_t j = 0; j < i; ++j) if (candidate.players_[j].id == p.id) return false;
    }
    for (size_t i = 0; i < qc; ++i) {
      WinningLine& q = candidate.quotes_[i];
      q.player = get(bytes,pos,8);q.seen = get(bytes,pos,4);
      std::memcpy(q.sentence,bytes+pos,kSentenceBytes+1);pos+=kSentenceBytes+1;
      if (q.sentence[kSentenceBytes] != 0 || !q.player) return false;
    }
    for (size_t i = 0; i < sc; ++i) {
      Pair p{};
      p.pack = get(bytes,pos,4);p.prompt = get(bytes,pos,2);p.answer = get(bytes,pos,2);
      p.count = static_cast<uint8_t>(get(bytes,pos,1));
      for (uint64_t& id : p.voters) id = get(bytes,pos,8);
      if (!p.count || p.count > 3 || p.prompt == UINT16_MAX || p.answer == UINT16_MAX) return false;
      for (uint8_t j = 0; j < p.count; ++j) {
        if (!p.voters[j]) return false;
        for (uint8_t k = 0; k < j; ++k) if (p.voters[j] == p.voters[k]) return false;
      }
      for (const Pair& prev : candidate.pairs_)
        if (prev.pack == p.pack && prev.prompt == p.prompt && prev.answer == p.answer) return false;
      candidate.pairs_.push_back(p);
    }
    *this = std::move(candidate);
    return true;
  }

 private:
  static void put(std::vector<uint8_t>& b, uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(v >> (8*i)));
  }
  static uint64_t get(const uint8_t* b, size_t& pos, size_t n) {
    uint64_t out = 0;
    for (size_t i = 0; i < n; ++i) out |= static_cast<uint64_t>(b[pos++]) << (8*i);
    return out;
  }
  std::array<Player,kPlayersStored> players_{};
  std::array<WinningLine,kWinningLines> quotes_{};
  std::vector<Pair> pairs_;
  size_t count_ = 0, quoteCount_ = 0;
};
}  // namespace history
}  // namespace cardparty
