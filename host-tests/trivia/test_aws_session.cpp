#include <cassert>
#include <cstdio>
#include <cstring>
#include "AwsSession.h"

int main() {
  constexpr uint32_t fp = 0xa81343deu;
  constexpr uint32_t count = 150;
  trivia::AwsSession state;
  state.fingerprint = fp;
  state.count = count;
  state.current = 48;
  state.generation = 36;
  state.asked = 14;
  state.right = 9;
  state.view = trivia::kAwsSessionOption;
  state.option = 4;
  state.selected = 0x15;
  state.page = 7;
  state.review = 1;
  state.domains = 0x13;
  uint8_t bytes[trivia::kAwsSessionBytes];
  trivia::encodeAwsSession(state, bytes);
  trivia::AwsSession loaded;
  assert(trivia::decodeAwsSession(bytes, sizeof(bytes), fp, count, loaded));
  assert(loaded.current == 48 && loaded.asked == 14 && loaded.right == 9);
  assert(loaded.option == 4 && loaded.selected == 0x15 && loaded.page == 7);
  assert(loaded.view == trivia::kAwsSessionOption && loaded.review == 1);
  assert(loaded.domains == 0x13 && loaded.generation == 36);
  assert(!trivia::decodeAwsSession(bytes, sizeof(bytes) - 1, fp, count, loaded));
  assert(!trivia::decodeAwsSession(bytes, sizeof(bytes), fp + 1, count, loaded));
  assert(!trivia::decodeAwsSession(bytes, sizeof(bytes), fp, count + 1, loaded));
  uint8_t broken[trivia::kAwsSessionBytes];
  std::memcpy(broken, bytes, sizeof(broken));
  broken[11] ^= 1;  // Torn write => checksum mismatch.
  assert(!trivia::decodeAwsSession(broken, sizeof(broken), fp, count, loaded));
  trivia::AwsSession invalid = state;
  invalid.current = count;
  trivia::encodeAwsSession(invalid, broken);
  assert(!trivia::decodeAwsSession(broken, sizeof(broken), fp, count, loaded));
  invalid = state;
  invalid.asked = 0;
  invalid.view = trivia::kAwsSessionExplanation;
  trivia::encodeAwsSession(invalid, broken);
  assert(!trivia::decodeAwsSession(broken, sizeof(broken), fp, count, loaded));
  invalid = state;
  invalid.selected = 0x80;
  trivia::encodeAwsSession(invalid, broken);
  assert(!trivia::decodeAwsSession(broken, sizeof(broken), fp, count, loaded));
  trivia::AwsSession explanation = state;
  explanation.view = trivia::kAwsSessionExplanation;
  explanation.correct = 1;
  trivia::encodeAwsSession(explanation, bytes);
  assert(trivia::decodeAwsSession(bytes, sizeof(bytes), fp, count, loaded));
  assert(loaded.asked == state.asked && loaded.view == trivia::kAwsSessionExplanation);
  trivia::AwsSession summary = state;
  summary.view = trivia::kAwsSessionSummary;
  summary.hasQuestion = 0;
  trivia::encodeAwsSession(summary, bytes);
  assert(trivia::decodeAwsSession(bytes, sizeof(bytes), fp, count, loaded));
  assert(!loaded.hasQuestion);
  assert(trivia::awsSessionNewer(1u, 0xffffffffu));
  assert(!trivia::awsSessionNewer(0xffffffffu, 1u));
  std::puts("PASS AWS checkpoint codec, corruption, pack change and score restore");
  return 0;
}
