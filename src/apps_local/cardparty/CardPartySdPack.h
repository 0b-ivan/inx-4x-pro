#pragma once
// Versioned read-only CAW SD pack. No Arduino dependency: host-testable.
// A release flag is only a safety gate, NOT proof of rights or authenticity.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "CardPartyCore.h"

namespace cardparty { namespace sdpack {
constexpr uint32_t kMaxPackBytes=192u*1024u;
constexpr uint16_t kMaxPromptBytes=384, kMaxAnswerBytes=160;
constexpr uint32_t kHeaderBytes=32;
constexpr uint16_t kFormatVersion=1, kReleasedFlag=1;

class ByteSource {
 public:
  virtual ~ByteSource()=default;
  virtual uint32_t size() const=0;
  // An incomplete read returns false; partial data may never be accepted.
  virtual bool read(uint32_t offset,void* dst,uint32_t count)=0;
};
inline uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1])<<8));
}
inline uint32_t read32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1])<<8) |
         (static_cast<uint32_t>(p[2])<<16) |
         (static_cast<uint32_t>(p[3])<<24);
}
inline uint32_t crc32(const uint8_t* data,size_t size) {
  uint32_t crc=0xffffffffu;
  for(size_t i=0;i<size;++i) {
    crc ^= data[i];
    for(int bit=0;bit<8;++bit)
      crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
  }
  return ~crc;
}
inline bool validText(const uint8_t* p,size_t len) {
  if(!len)return false;
  for(size_t i=0;i<len;) {
    const uint8_t c=p[i];
    if(c<0x20 || c==0x7f)return false;
    if(c<0x80){++i;continue;}
    unsigned extra=0;
    if(c>=0xc2&&c<=0xdf)extra=1;
    else if(c>=0xe0&&c<=0xef)extra=2;
    else if(c>=0xf0&&c<=0xf4)extra=3;
    else return false;
    if(i+extra>=len)return false;
    for(unsigned j=1;j<=extra;++j)
      if((p[i+j]&0xc0)!=0x80)return false;
    if((c==0xe0&&p[i+1]<0xa0)||(c==0xed&&p[i+1]>=0xa0)||
       (c==0xf0&&p[i+1]<0x90)||(c==0xf4&&p[i+1]>0x8f))return false;
    i+=extra+1;
  }
  return true;
}
inline uint8_t placeholders(const char* str,uint16_t len) {
  uint8_t n=0;
  for(uint16_t i=0;i+3<len;++i)
    if(std::memcmp(str+i,"____",4)==0){if(n==255)return n;++n;i+=3;}
  return n;
}

class Pack final {
 public:
  Pack()=default;
  Pack(const Pack&)=delete;
  Pack& operator=(const Pack&)=delete;
  // Call only before Game::configure; no hot reload while game uses text ptrs.
  bool load(ByteSource& source,bool allowDraft=false) {
    const uint32_t size=source.size();
    if(size<kHeaderBytes || size>kMaxPackBytes)return false;
    uint8_t header[kHeaderBytes]={};
    if(!source.read(0,header,kHeaderBytes))return false;
    const uint8_t magic[8]={'X','C','A','W','P','K','1',0};
    if(std::memcmp(magic,header,8)!=0||read16(header+8)!=kFormatVersion)return false;
    const uint16_t flags=read16(header+10);
    if((flags & ~kReleasedFlag)!=0||(!allowDraft && !(flags & kReleasedFlag)))return false;
    const uint16_t pCount=read16(header+12),aCount=read16(header+14);
    if(!pCount||pCount>4096||aCount<32||aCount>4096)return false;
    const uint32_t bytes=read32(header+16);
    if(bytes!=size-kHeaderBytes||read32(header+24)==0||read32(header+28)!=0)return false;
    std::unique_ptr<uint8_t[]> blob(new(std::nothrow)uint8_t[bytes]);
    std::unique_ptr<Prompt[]> prompts(new(std::nothrow)Prompt[pCount]);
    std::unique_ptr<Answer[]> answers(new(std::nothrow)Answer[aCount]);
    if(!blob||!prompts||!answers||!source.read(kHeaderBytes,blob.get(),bytes))return false;
    if(crc32(blob.get(),bytes)!=read32(header+20))return false;
    uint32_t offset=0;
    for(uint16_t i=0;i<pCount;++i) {
      if(bytes-offset<4)return false;
      const uint16_t n=read16(blob.get()+offset);
      const uint8_t pick=blob[offset+2],draw=blob[offset+3];
      offset+=4;
      if(!n||n>kMaxPromptBytes||bytes-offset<=n||pick<1||pick>3||
         draw>2||blob[offset+n]!=0||!validText(blob.get()+offset,n))return false;
      const char* text=reinterpret_cast<const char*>(blob.get()+offset);
      const uint8_t placeholdersFound=placeholdersCount(text,n);
      // No slots: question stands alone; answers display separately.
      if(placeholdersFound && placeholdersFound!=pick)return false;
      prompts[i]=Prompt{text,pick,draw};
      offset+=n+1;
    }
    for(uint16_t i=0;i<aCount;++i) {
      if(bytes-offset<2)return false;
      const uint16_t n=read16(blob.get()+offset);
      offset+=2;
      if(!n||n>kMaxAnswerBytes||bytes-offset<=n||
         blob[offset+n]!=0||!validText(blob.get()+offset,n))return false;
      const char* text=reinterpret_cast<const char*>(blob.get()+offset);
      if(placeholdersCount(text,n))return false;
      answers[i]=Answer{text};
      offset+=n+1;
    }
    if(offset!=bytes)return false;
    // Atomic commit: corrupt or truncated new pack never replaces a good one.
    blob_=std::move(blob);prompts_=std::move(prompts);answers_=std::move(answers);
    deck_=Deck{prompts_.get(),pCount,answers_.get(),aCount};
    version_=read32(header+24);
    return true;
  }
  const Deck& deck() const{return deck_;}
  uint32_t version() const{return version_;}
  bool ready() const{return deck_.promptsCount>0&&deck_.answersCount>0;}
 private:
  static uint8_t placeholdersCount(const char* text,uint16_t n){
    return sdpack::placeholders(text,n);
  }
  std::unique_ptr<uint8_t[]> blob_;
  std::unique_ptr<Prompt[]> prompts_;
  std::unique_ptr<Answer[]> answers_;
  Deck deck_{};
  uint32_t version_=0;
};

}} // namespace cardparty::sdpack
