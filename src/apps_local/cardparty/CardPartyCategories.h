#pragma once
// Category selection for SD packs: which cards a session plays with.
// No Arduino dependency: host-testable. The device persists the selection.
#include <cstdint>
#include <memory>
#include <new>
#include "CardPartyCore.h"
#include "CardPartySdPack.h"

namespace cardparty { namespace categories {

// A card plays only if none of its categories is switched off, so a card
// tagged "Politik + FSK 18" stays out while FSK 18 is off. Untagged cards
// always play.
inline bool included(uint16_t cardMask,uint16_t enabled) { return (cardMask & ~enabled)==0; }

// First-run selection: the pack's defaults; adult categories are always off.
inline uint16_t defaults(const sdpack::Pack& pack) {
  uint16_t mask=0;
  for(uint8_t i=0;i<pack.categoryCount();++i)
    if(pack.category(i).defaultOn && !pack.category(i).adult) mask|=static_cast<uint16_t>(1u<<i);
  return mask;
}

// Distinct answers a full table needs: eight hands, the largest draw for
// everyone but the Czar, and Rando's three. Mirrors Game::start().
inline uint16_t requiredAnswers(const Deck& deck,uint8_t hand) {
  uint8_t maxDraw=0;
  for(uint16_t i=0;i<deck.promptsCount;++i)
    if(deck.prompts[i].draw>maxDraw)maxDraw=deck.prompts[i].draw;
  return static_cast<uint16_t>(kPlayers*hand+(kPlayers-1)*maxDraw+3);
}
inline bool playable(const Deck& deck,uint8_t hand) {
  return deck.promptsCount>0 && deck.answersCount>=requiredAnswers(deck,hand);
}

// Cards of the pack that pass the selection, in pack order. Text pointers
// stay owned by the pack, so the pack must outlive this deck.
class FilteredDeck final {
 public:
  bool build(const sdpack::Pack& pack,uint16_t enabled) {
    const Deck& all=pack.deck();
    uint16_t p=0,a=0;
    for(uint16_t i=0;i<all.promptsCount;++i) if(included(pack.promptMask(i),enabled)) ++p;
    for(uint16_t i=0;i<all.answersCount;++i) if(included(pack.answerMask(i),enabled)) ++a;
    std::unique_ptr<Prompt[]> prompts(new(std::nothrow)Prompt[p?p:1]);
    std::unique_ptr<Answer[]> answers(new(std::nothrow)Answer[a?a:1]);
    if(!prompts||!answers) return false;
    p=a=0;
    for(uint16_t i=0;i<all.promptsCount;++i)
      if(included(pack.promptMask(i),enabled)) prompts[p++]=all.prompts[i];
    for(uint16_t i=0;i<all.answersCount;++i)
      if(included(pack.answerMask(i),enabled)) answers[a++]=all.answers[i];
    prompts_=std::move(prompts);
    answers_=std::move(answers);
    deck_=Deck{prompts_.get(),p,answers_.get(),a};
    return true;
  }
  const Deck& deck() const { return deck_; }
 private:
  std::unique_ptr<Prompt[]> prompts_;
  std::unique_ptr<Answer[]> answers_;
  Deck deck_{};
};

// Cards carrying category i, whatever else is switched on.
inline uint16_t cardsIn(const sdpack::Pack& pack,uint8_t i,bool answers) {
  const uint16_t bit=static_cast<uint16_t>(1u<<i);
  const uint16_t n=answers?pack.deck().answersCount:pack.deck().promptsCount;
  uint16_t count=0;
  for(uint16_t c=0;c<n;++c)
    if((answers?pack.answerMask(c):pack.promptMask(c)) & bit) ++count;
  return count;
}

// Saved selection: 8 bytes, keyed to the pack's CRC so a new or changed
// pack starts from its own defaults instead of a stale bit pattern.
constexpr uint8_t kSelectionBytes=8;
inline void encode(uint32_t packCrc,uint16_t mask,uint8_t out[kSelectionBytes]) {
  out[0]='C';out[1]='S';
  for(int i=0;i<4;++i) out[2+i]=static_cast<uint8_t>(packCrc>>(8*i));
  out[6]=static_cast<uint8_t>(mask);out[7]=static_cast<uint8_t>(mask>>8);
}
inline bool decode(const uint8_t in[kSelectionBytes],uint32_t packCrc,uint8_t categoryCount,uint16_t& mask) {
  if(in[0]!='C'||in[1]!='S'||sdpack::read32(in+2)!=packCrc) return false;
  const uint16_t saved=sdpack::read16(in+6);
  const uint16_t valid=static_cast<uint16_t>((1u<<categoryCount)-1u);
  if(saved & ~valid) return false;
  mask=saved;
  return true;
}

}} // namespace cardparty::categories
