#pragma once
// Browser and X4 share this Pick 1/2/3 text-composition contract.
// Never interprets card text as markup; caller controls highlighting.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cardparty {
namespace sentence {
inline bool blank(const char* p, size_t& bytes) {
  bytes = 0;
  if (!p || !*p) return false;
  if (p[0] == '_') {
    while (p[bytes] == '_') ++bytes;
    return bytes >= 3;
  }
  if (std::strncmp(p,"[blank]",7) == 0 || std::strncmp(p,"[BLANK]",7) == 0) {
    bytes=7;return true;
  }
  if (std::strncmp(p,"{blank}",7) == 0 || std::strncmp(p,"{BLANK}",7) == 0) {
    bytes=7;return true;
  }
  return false;
}
class Writer {
 public:
  Writer(char* out,size_t cap):out_(out),cap_(cap) {if(cap_)out_[0]=0;}
  void add(const char* text,size_t length) {
    if(!out_||!cap_||!text)return;
    size_t room=cap_-1-used_;
    if(length>room)length=room;
    // Never cut UTF-8 between codepoint bytes.
    if(length && text[length]) {
      while(length && (static_cast<uint8_t>(text[length])&0xc0u)==0x80u)--length;
    }
    std::memcpy(out_+used_,text,length);
    used_+=length;out_[used_]=0;
  }
  void add(const char* text) {if(text)add(text,std::strlen(text));}
  size_t size() const {return used_;}
 private:
  char* out_;
  size_t cap_,used_=0;
};
inline size_t compose(const char* prompt,const char* const* answers,uint8_t pick,
                      char* out,size_t capacity) {
  Writer writer(out,capacity);
  if(!prompt)return 0;
  uint8_t used=0;
  bool found=false;
  for(const char* p=prompt;*p;) {
    size_t n=0;
    if(blank(p,n)) {
      found=true;
      if(answers&&used<pick&&answers[used])writer.add(answers[used++]);
      else writer.add(p,n);
      p+=n;
    } else {
      // Advance by one complete codepoint, so long German card text
      // never loses an initial byte or emits an orphan continuation byte.
      const uint8_t lead=static_cast<uint8_t>(*p);
      const size_t requested=lead<0x80u?1u:(lead&0xe0u)==0xc0u?2u:
          (lead&0xf0u)==0xe0u?3u:(lead&0xf8u)==0xf0u?4u:1u;
      size_t count=1;
      while(count<requested && p[count] &&
            (static_cast<uint8_t>(p[count])&0xc0u)==0x80u)++count;
      writer.add(p,count);
      p+=count;
    }
  }
  if(!found||used<pick) {
    while(answers&&used<pick) {
      writer.add(" ");
      if(answers[used])writer.add(answers[used]);
      ++used;
    }
  }
  return writer.size();
}
} // namespace sentence
} // namespace cardparty
