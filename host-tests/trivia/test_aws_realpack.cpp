// Verify that the Python AWS study-pack builder and C++ device reader agree.
// Test data is generated synthetically; never check licensed quiz text into Git.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstring>

#include "AwsStudyPack.h"

class InMemorySource final : public trivia::ByteSource {
 public:
  explicit InMemorySource(const char* filename) {
    std::ifstream in(filename, std::ios::binary);
    bytes_ = std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
  }
  bool read(uint32_t offset, void* dst, uint32_t length) override {
    if (offset > bytes_.size() || length > bytes_.size() - offset) return false;
    if (length) std::memcpy(dst, bytes_.data() + offset, length);
    return true;
  }
  uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
 private:
  std::vector<uint8_t> bytes_;
};

int main(int argc, char* argv[]) {
  if (argc != 2) { std::fputs("usage: test_aws_realpack path\n", stderr); return 2; }
  InMemorySource source(argv[1]);
  trivia::AwsPack pack;
  if (!pack.open(source) || pack.count() != 150 || pack.fingerprint() == 0) {
    std::fputs("AWS synthetic pack failed to open\n", stderr);
    return 1;
  }
  trivia::AwsQuestion q;
  int singles = 0;
  int multis = 0;
  int domainCount[6] = {};
  for (uint32_t i = 0; i < pack.count(); ++i) {
    if (!pack.read(i, q) || q.domain() != (i % 6) + 1 ||
        q.optionCount() != 6 || std::strlen(q.question()) < 300 ||
        std::strlen(q.explanation()) < 60 ||
        std::strcmp(q.option(0), "Amazon S3") != 0) {
      std::fprintf(stderr, "Incorrect synthetic AWS question at %u\n", i);
      return 1;
    }
    ++domainCount[q.domain() - 1];
    const uint8_t expectedMask = i % 4 == 0 ? 0x05 : 0x02;
    if (q.correctMask() != expectedMask || !q.evaluate(expectedMask) ||
        q.evaluate(0) || q.evaluate(0x03)) {
      std::fprintf(stderr, "Incorrect answer mask at %u\n", i);
      return 1;
    }
    if (q.multiple()) ++multis;
    else ++singles;
    // Long scenarios paginate without losing a byte or splitting UTF-8.
    if (i == 0) {
      const char* full = q.question();
      uint32_t cursor = 0;
      int pages = 0;
      while (true) {
        auto p = trivia::awsTextPage(full, cursor, 120);
        if (p.length == 0 || p.start != cursor) return 1;
        cursor += p.length;
        ++pages;
        if (!p.more) break;
        if (pages > 200) return 1;
      }
      if (cursor != std::strlen(full) || pages < 3) return 1;
    }
  }
  for (int count : domainCount) if (count != 25) return 1;
  if (singles != 112 || multis != 38) return 1;
  std::printf("PASS Python/C++ AWS study pack: 150 questions, 6 domains, %d multi-response\n", multis);
  return 0;
}
