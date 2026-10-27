// Native regression suite for the SAME read-only pack loader as the X4.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "CardPartyCore.h"
#include "CardPartySdPack.h"

using namespace cardparty;
using namespace cardparty::sdpack;

struct Buffer : ByteSource {
  std::vector<uint8_t> bytes;
  uint32_t size() const override {return static_cast<uint32_t>(bytes.size());}
  bool read(uint32_t offset,void* dst,uint32_t n) override {
    if(offset>bytes.size() || n>bytes.size()-offset)return false;
    std::memcpy(dst,bytes.data()+offset,n);return true;
  }
};
static void u16(std::vector<uint8_t>& v,uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));v.push_back(static_cast<uint8_t>(x>>8));
}
static void u32(std::vector<uint8_t>& v,uint32_t x) {
  for(unsigned i=0;i<4;++i)v.push_back(static_cast<uint8_t>(x>>(i*8)));
}
static void str(std::vector<uint8_t>& v,const std::string& s) {
  u16(v,static_cast<uint16_t>(s.size()));
  v.insert(v.end(),s.begin(),s.end());v.push_back(0);
}
static Buffer fixture(bool released=true) {
  Buffer out;
  std::vector<uint8_t> body;
  const char first[]="Ohne Lücke?";
  u16(body,static_cast<uint16_t>(std::strlen(first)));
  body.push_back(1);body.push_back(0);
  body.insert(body.end(),first,first+std::strlen(first));body.push_back(0);
  const char second[]="____ ____";
  u16(body,static_cast<uint16_t>(std::strlen(second)));
  body.push_back(2);body.push_back(0);
  body.insert(body.end(),second,second+std::strlen(second));body.push_back(0);
  for(int i=0;i<100;++i)str(body,"Eine Antwort "+std::to_string(i));
  out.bytes={'X','C','A','W','P','K','1',0};
  u16(out.bytes,1);u16(out.bytes,released?1:0);
  u16(out.bytes,2);u16(out.bytes,100);
  u32(out.bytes,static_cast<uint32_t>(body.size()));
  u32(out.bytes,crc32(body.data(),body.size()));
  u32(out.bytes,1);u32(out.bytes,0);
  out.bytes.insert(out.bytes.end(),body.begin(),body.end());
  return out;
}
static void refreshCrc(Buffer& data){
  const uint32_t sum=crc32(data.bytes.data()+32,data.bytes.size()-32);
  for(int i=0;i<4;++i)data.bytes[20+i]=static_cast<uint8_t>(sum>>(i*8));
}
int main() {
  const auto good=fixture();
  Pack pack;Buffer input=good;
  assert(pack.load(input)&&pack.ready());
  assert(pack.deck().promptsCount==2&&pack.deck().answersCount==100);
  assert(std::strcmp(pack.deck().prompts[0].text,"Ohne Lücke?")==0);
  assert(pack.deck().prompts[1].pick==2);
  Game game;
  assert(game.configure(pack.deck(),Rules{5,10,true},1));
  assert(game.join("A")==0&&game.join("B")==1&&game.join("C")==2);
  assert(game.start()); // zero-placeholder prompt participates in a real game
  Buffer bad=good;bad.bytes[0]='?';assert(!pack.load(bad));
  bad=good;bad.bytes.back()^=0x20;assert(!pack.load(bad));
  bad=good;bad.bytes[10]=2;assert(!pack.load(bad));
  bad=good;bad.bytes.push_back(0);assert(!pack.load(bad));
  bad=good;bad.bytes[24]=0;assert(!pack.load(bad));
  bad=good;bad.bytes[12]=0xff;bad.bytes[13]=0xff;assert(!pack.load(bad));
  bad=good;bad.bytes.resize(40);assert(!pack.load(bad));
  bad=fixture(false);assert(!pack.load(bad)); // refuse unapproved/draft
  // Do not reload a live Game: this explicitly exercises offline test mode.
  assert(pack.load(bad,true));
  bad=good;bad.bytes[36]=0xc0;refreshCrc(bad);assert(!pack.load(bad));
  bad=good;bad.bytes[36]=0;refreshCrc(bad);assert(!pack.load(bad));
  bad=good;
  const size_t secondPick=32+4+std::strlen("Ohne Lücke?")+1+2;
  bad.bytes[secondPick]=3;refreshCrc(bad);assert(!pack.load(bad));
  std::puts("cardparty sd pack: game, no blank, pick2, CRC, UTF8, draft gate, malformed headers OK");
  return 0;
}
