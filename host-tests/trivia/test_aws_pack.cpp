// Binary ABI, category filtering, exact multi-answer grading and paging.
// Synthetic questions only: never store licensed exam content in a test.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AwsStudyPack.h"

using namespace trivia;

class MemorySource final : public WritableByteSource {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  bool read(uint32_t at, void* dest, uint32_t n) override {
    if (at > bytes_.size() || n > bytes_.size() - at) return false;
    std::memcpy(dest, bytes_.data() + at, n);
    return true;
  }
  bool write(uint32_t at, const void* src, uint32_t n) override {
    if (at > bytes_.size() || n > bytes_.size() - at) return false;
    std::memcpy(bytes_.data() + at, src, n);
    return true;
  }
  bool flush() override { return true; }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
  void poke(size_t at, uint8_t byte) { bytes_[at] = byte; }
 private:
  std::vector<uint8_t> bytes_;
};

void put32(std::vector<uint8_t>& into, uint32_t val) {
  for (int i = 0; i < 4; ++i) into.push_back(static_cast<uint8_t>(val >> (i * 8)));
}
std::vector<uint8_t> record(int domain, uint8_t mask) {
  std::vector<uint8_t> out{
      static_cast<uint8_t>(domain), 4, mask, 0};
  for (const char* text : {"Which AWS service stores objects?", "The service stores objects.",
                           "Amazon S3", "Amazon EBS", "Amazon EFS", "Amazon FSx"}) {
    while (*text) out.push_back(static_cast<uint8_t>(*text++));
    out.push_back(0);
  }
  return out;
}
std::vector<uint8_t> pack(const std::vector<std::vector<uint8_t>>& records) {
  std::vector<uint8_t> out(kAwsMagic, kAwsMagic + sizeof(kAwsMagic));
  out.push_back(1); out.push_back(0); out.push_back(0); out.push_back(0);
  put32(out, static_cast<uint32_t>(records.size()));
  put32(out, 0x1234abcd);  // synthetic fixture content fingerprint
  uint32_t total = 0;
  for (const auto& rec : records) {
    put32(out, total);
    total += static_cast<uint32_t>(rec.size());
  }
  put32(out, total);
  for (const auto& rec : records) out.insert(out.end(), rec.begin(), rec.end());
  return out;
}

void checkGoodPack() {
  MemorySource content(pack({record(1, 1), record(3, 5), record(6, 8)}));
  AwsPack reader;
  assert(reader.open(content));
  assert(reader.count() == 3);
  assert(reader.fingerprint() == 0x1234abcdu);
  uint8_t domain = 0;
  assert(reader.domainAt(1, domain) && domain == 3);
  AwsQuestion question;
  assert(reader.read(1, question));
  assert(question.domain() == 3 && question.optionCount() == 4);
  assert(question.multiple() && question.correctMask() == 5);
  assert(question.evaluate(5) && !question.evaluate(1) && !question.evaluate(7));
  assert(std::strcmp(question.option(0), "Amazon S3") == 0);
  assert(std::strcmp(question.explanation(), "The service stores objects.") == 0);
  assert(reader.read(2, question));
  assert(!question.multiple());
  assert(question.evaluate(8) && !question.evaluate(9));
  assert(!reader.read(3, question));

  MemorySource flags(std::vector<uint8_t>(3, 0));
  PackState state;
  assert(state.open(flags, 3));
  Rng rng(9);
  AwsChooser chooser;
  chooser.begin(reader, state, rng);
  uint32_t id = 9;
  for (int i = 0; i < 10; ++i) {
    assert(chooser.next(id, 1u << 2) && id == 1); // only Domain 3
  }
  assert(!chooser.next(id, 0));
  assert(state.setFlag(1, kSeen | kAwsMissed));
  assert(chooser.next(id, 1u << 2, true) && id == 1);
  assert(!chooser.next(id, 1u << 5, true));
  assert(state.setFlag(1, kFlagged));
  assert(!chooser.next(id, 1u << 2));
}

void checkBadPacks() {
  const auto good = pack({record(1, 1)});
  {
    auto data = good; data[0] = 'Q';
    MemorySource src(std::move(data));
    AwsPack reader; assert(!reader.open(src));
  }
  {
    auto data = good; data.pop_back();
    MemorySource src(std::move(data));
    AwsPack reader; assert(!reader.open(src));
  }
  {
    auto data = good; data[20] = 255; // corrupt first offset
    MemorySource src(std::move(data)); AwsPack reader;
    assert(reader.open(src)); AwsQuestion q; assert(!reader.read(0, q));
  }
  {
    MemorySource src(pack({record(1, 15)})); // no wrong choice
    AwsPack reader; assert(reader.open(src));
    AwsQuestion q; assert(!reader.read(0, q));
  }
  {
    auto row = record(1, 1); row.back() = 'x'; // missing final NUL
    MemorySource src(pack({row})); AwsPack reader;
    assert(reader.open(src)); AwsQuestion q; assert(!reader.read(0, q));
  }
  {
    auto row = record(7, 1); MemorySource src(pack({row}));
    AwsPack reader; assert(reader.open(src));
    AwsQuestion q; assert(!reader.read(0, q));
  }
  {
    auto row = record(1, 1); row.insert(row.end(), 12288, 'x');
    MemorySource src(pack({row})); AwsPack reader;
    assert(reader.open(src)); AwsQuestion q; assert(!reader.read(0, q));
  }
}

void checkPaging() {
  const char text[] = "abc\xc3\xa4Z"; // ä is two UTF-8 bytes
  auto a = awsTextPage(text, 0, 4);
  assert(a.start == 0 && a.length == 5 && a.more);
  auto b = awsTextPage(text, a.start + a.length, 4);
  assert(b.start == 5 && b.length == 1 && !b.more);
  auto c = awsTextPage(text, 4, 1); // continuation byte: rewind
  assert(c.start == 3 && c.length == 2 && c.more);
}

int main() {
  checkGoodPack();
  checkBadPacks();
  checkPaging();
  std::puts("AWS study pack host tests passed");
}
