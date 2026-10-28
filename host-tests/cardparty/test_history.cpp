#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../../src/apps_local/cardparty/CardPartyHistory.h"

using namespace cardparty::history;

int main() {
  Book book;
  const uint64_t alice = UINT64_C(0x11112222deadbeef);
  const uint64_t bob = UINT64_C(0x33334444deadbeef);  // same truncated 32-bit ID
  const uint64_t carol = UINT64_C(0x55556666deadbeef);
  const uint64_t dave = UINT64_C(0x77778888deadbeef);
  assert(book.upsert(alice,"Alice",10));
  assert(book.upsert(bob,"Bob",11));
  assert(book.upsert(carol,"Carol",12));
  assert(book.player(alice)->id != book.player(bob)->id);
  assert(book.player(alice)->word == Book::wordFor(alice));
  assert(book.upsert(alice,"Alice",15,7));
  assert(book.player(alice)->word == 7);
  assert(!book.upsert(0,"Invalid",13));
  assert(!book.upsert(alice,"",13));
  assert(book.played(alice));
  assert(book.gameWon(alice));
  assert(book.roundWon(alice,"Drei vergessene Backups",20));
  assert(book.player(alice)->games == 1 && book.player(alice)->wins == 1);
  assert(book.player(alice)->rounds == 1 && book.quotes() == 1);
  assert(book.quote(0).player == alice);
  assert(std::strcmp(book.quote(0).sentence,"Drei vergessene Backups") == 0);

  constexpr uint32_t pack = 0xabcde123;
  assert(book.strike(alice,pack,4,17) == Book::Vote::Recorded);
  assert(book.strike(alice,pack,4,17) == Book::Vote::Duplicate);
  assert(book.strike(bob,pack,4,17) == Book::Vote::Recorded);
  assert(!book.blocked(pack,4,17));
  assert(book.strike(carol,pack,4,17) == Book::Vote::Blocked);
  assert(book.blocked(pack,4,17));
  assert(!book.blocked(pack+1,4,17));
  assert(!book.blocked(pack,4,18));
  assert(!book.blocked(pack,5,17));
  assert(book.strike(dave,pack,4,17) == Book::Vote::Blocked);
  assert(book.strike(0,pack,4,17) == Book::Vote::Invalid);
  assert(book.strike(dave,pack,UINT16_MAX,17) == Book::Vote::Invalid);

  const auto saved = book.encode();
  Book restored;
  assert(restored.decode(saved.data(),saved.size()));
  assert(restored.players()==3 && restored.quotes()==1 && restored.pairs()==1);
  assert(restored.blocked(pack,4,17));
  assert(restored.player(alice)->wins == 1);
  assert(restored.player(alice)->word == 7);
  assert(restored.strike(alice,pack,4,17) == Book::Vote::Blocked);
  auto bad = saved;
  bad[12] ^= 0x80;
  assert(!restored.decode(bad.data(),bad.size()));
  assert(restored.player(alice)->wins == 1); // no mutation on a corrupt file
  bad = saved;
  bad[4]=2; // unsupported future version must fail closed
  assert(!restored.decode(bad.data(),bad.size()));
  assert(!restored.decode(saved.data(),saved.size()-1));

  // A cap of 20 retained quotes, newest first, including non-ASCII.
  for (uint32_t i=0;i<24;++i) assert(book.roundWon(alice,"Über den Wolken",100+i));
  assert(book.quotes()==20);
  assert(book.quote(0).seen==123);
  assert(book.quote(19).seen==104);

  // Bounded 64-identity cache: least recently seen record falls out.
  for (uint64_t id=100; id<165; ++id) assert(book.upsert(id,"Guest",static_cast<uint32_t>(id+100)));
  assert(book.players()==64);
  assert(book.player(alice)==nullptr);
  // Keep all 2-vote pairs when there are too many, evict 1-vote pairs first.
  Book votes;
  for (uint16_t answer=0; answer<kNonFitPairs; ++answer)
    assert(votes.strike(alice,pack,5,answer)==Book::Vote::Recorded);
  assert(votes.pairs()==kNonFitPairs);
  assert(votes.strike(bob,pack,5,0)==Book::Vote::Recorded);
  assert(votes.strike(alice,pack,7,8)==Book::Vote::Recorded);
  assert(votes.pairs()==kNonFitPairs);
  assert(votes.strike(carol,pack,5,0)==Book::Vote::Blocked);
  const auto large=votes.encode();
  Book again;
  assert(again.decode(large.data(),large.size()));
  assert(again.blocked(pack,5,0));
  assert(again.pairs()==kNonFitPairs);
}
