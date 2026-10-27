// Browser protocol regression: strict bounded JSON, no coerced commands.
#include <cstdio>
#include <cstring>
#include <string>
#include "web/CardPartyProtocol.h"
using namespace cardparty;
using namespace cardparty::web;
static int checked=0, failed=0;
#define CHECK(x) do {++checked;if(!(x)){++failed;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static bool decode(const std::string& s, Command& c) {
  return parse(reinterpret_cast<const uint8_t*>(s.data()),s.size(),c);
}
static void accepted() {
  Command c{};
  CHECK(decode(R"({"type":"join","name":"Ivan"})",c));
  CHECK(c.kind==Kind::Join&&std::strcmp(c.name,"Ivan")==0);
  CHECK(decode(R"({"type":"join","name":"Müller"})",c));
  CHECK(std::strcmp(c.name,"Müller")==0);
  CHECK(decode(R"({"type":"join","name":"A\"B"})",c));
  CHECK(std::strcmp(c.name,"A\"B")==0);
  const std::string secret="1234567890abcdefabcdef0123456789";
  CHECK(decode(std::string("{\"type\":\"resume\",\"token\":\"")+secret+"\"}",c));
  CHECK(c.kind==Kind::Resume);
  char roundTrip[33]={};
  encodeSecret(c.secret,roundTrip);
  CHECK(std::strcmp(roundTrip,secret.c_str())==0);
  CHECK(decode(std::string("{\"type\":\"submit\",\"token\":\"")+secret+"\",\"revision\":12,\"slots\":[0]}",c));
  CHECK(c.kind==Kind::Submit&&c.count==1&&c.slots[0]==0&&c.revision==12);
  CHECK(decode(std::string("{\"type\":\"submit\",\"token\":\"")+secret+"\",\"revision\":0,\"slots\":[2,1]}",c));
  CHECK(c.count==2&&c.slots[0]==2&&c.slots[1]==1);
  CHECK(decode(std::string("{\"type\":\"submit\",\"token\":\"")+secret+"\",\"revision\":1,\"slots\":[0,1,2]}",c));
  CHECK(c.count==3&&c.slots[2]==2);
  CHECK(decode(std::string("{\"type\":\"judge\",\"token\":\"")+secret+"\",\"revision\":400,\"selected\":7}",c));
  CHECK(c.kind==Kind::Judge&&c.selected==7&&c.revision==400);
}
static void rejected() {
  Command c{};
  const std::string t="1234567890abcdefabcdef0123456789";
  const char* const invalid[]={
    "", "[]", "null", "false", "{}", "{\"type\":\"join\"}",
    R"({"type":"join","name":""})",
    R"({"type":"join","name":"0123456789012345678901234"})",
    R"({"type":"join","name":"bad\nname"})",
    R"({"type":"join","name":"\u003cscript\u003e"})",
    R"({"type":"join","name":"<script>"})",
    R"({"type":"join","name":"X","role":"admin"})",
    R"({"type":"join","name":"X","type":"join"})",
    R"({"type":"join","name":42})",
    R"({"type":"join","name":"X"} garbage)",
    R"({"type":"join","name":"X","token":"Z"})",
    R"({"type":"submit","token":"00","revision":0,"slots":[0]})",
    R"({"type":"resume","token":"00000000000000000000000000000000"})",
    R"({"type":"resume","token":"ABCDEF0123456789abcdef0123456789"})",
    R"({"type":"anything","name":"X"})",
    R"({"type":"judge","token":"00","revision":1,"selected":1})"
  };
  for(const auto* x:invalid)CHECK(!decode(x,c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":-1,\"slots\":[0]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":1.2,\"slots\":[0]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":01,\"slots\":[0]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":0,\"slots\":[]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":0,\"slots\":[0,1,2,3]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":0,\"slots\":[12]}",c));
  CHECK(!decode("{\"type\":\"submit\",\"token\":\""+t+"\",\"revision\":0,\"slots\":[-1]}",c));
  CHECK(!decode("{\"type\":\"judge\",\"token\":\""+t+"\",\"revision\":0,\"selected\":8}",c));
  CHECK(!decode("{\"type\":\"judge\",\"token\":\""+t+"\",\"revision\":0,\"selected\":true}",c));
  CHECK(!decode("{\"type\":\"judge\",\"token\":\""+t+"\",\"revision\":4294967296,\"selected\":0}",c));
  CHECK(!decode("{\"type\":\"resume\",\"token\":\""+t+"\",\"type\":\"join\"}",c));
  std::string withNul="{\"type\":\"join\",\"name\":\"A\"}";
  withNul.push_back('\0');
  CHECK(!decode(withNul,c));
  std::string longFrame(193,'x');
  CHECK(!decode(longFrame,c));
}
static void writer() {
  char buf[128]={};
  JsonWriter w(buf,sizeof(buf));
  CHECK(w.raw("{\"value\":"));
  CHECK(w.quoted("a\"b\\c\n"));
  CHECK(w.raw(",\"number\":"));
  CHECK(w.num(4294967295u));
  CHECK(w.raw("}"));
  CHECK(std::strcmp(buf,"{\"value\":\"a\\\"b\\\\c\\u000a\",\"number\":4294967295}")==0);
  CHECK(w.size()==std::strlen(buf));
  char tiny[4]={};JsonWriter limited(tiny,sizeof(tiny));
  CHECK(!limited.quoted("Too large"));
  CHECK(limited.size()==0);
}
int main(){accepted();rejected();writer();
 std::printf("cardparty protocol: %d checks, %d failures\n",checked,failed);
 return failed?1:0;}
