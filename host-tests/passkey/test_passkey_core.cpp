#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "passkey/PasskeyCbor.h"
#include "passkey/PasskeyPresence.h"

namespace {

void testCborRoundTrip() {
  std::array<uint8_t, 96> encoded{};
  passkey::CborWriter writer(encoded.data(), encoded.size());
  const std::array<uint8_t, 3> bytes{0x01, 0x02, 0x03};

  assert(writer.putMap(4));
  assert(writer.putUnsigned(1));
  assert(writer.putSigned(-7));
  assert(writer.putUnsigned(2));
  assert(writer.putBytes(bytes.data(), bytes.size()));
  assert(writer.putUnsigned(3));
  assert(writer.putText("public-key", 10));
  assert(writer.putUnsigned(4));
  assert(writer.putBool(true));

  passkey::CborReader reader(encoded.data(), writer.size());
  std::size_t fields = 0;
  assert(reader.readMap(fields) && fields == 4);

  uint64_t key = 0;
  int64_t signedValue = 0;
  assert(reader.readUnsigned(key) && key == 1);
  assert(reader.readSigned(signedValue) && signedValue == -7);

  const uint8_t* readBytes = nullptr;
  std::size_t readBytesLength = 0;
  assert(reader.readUnsigned(key) && key == 2);
  assert(reader.readBytes(readBytes, readBytesLength) && readBytesLength == bytes.size());
  assert(std::memcmp(readBytes, bytes.data(), bytes.size()) == 0);

  const char* text = nullptr;
  std::size_t textLength = 0;
  assert(reader.readUnsigned(key) && key == 3);
  assert(reader.readText(text, textLength) && textLength == 10);
  assert(std::memcmp(text, "public-key", 10) == 0);

  bool flag = false;
  assert(reader.readUnsigned(key) && key == 4);
  assert(reader.readBool(flag) && flag);
  assert(reader.finished());
}

void testCborRejectsUnboundedOrMalformedInput() {
  {
    const uint8_t nonCanonical[] = {0x18, 0x17};
    passkey::CborReader reader(nonCanonical, sizeof(nonCanonical));
    uint64_t value = 0;
    assert(!reader.readUnsigned(value));
  }
  {
    const uint8_t indefiniteArray[] = {0x9f, 0xff};
    passkey::CborReader reader(indefiniteArray, sizeof(indefiniteArray));
    std::size_t count = 0;
    assert(!reader.readArray(count));
  }
  {
    const uint8_t tooManyItems[] = {0x98, 0x41};  // array(65)
    passkey::CborReader reader(tooManyItems, sizeof(tooManyItems));
    std::size_t count = 0;
    assert(!reader.readArray(count));
  }
  {
    const uint8_t truncatedBytes[] = {0x43, 0x01};
    passkey::CborReader reader(truncatedBytes, sizeof(truncatedBytes));
    const uint8_t* bytes = nullptr;
    std::size_t length = 0;
    assert(!reader.readBytes(bytes, length));
  }
  {
    const uint8_t tooDeep[] = {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x00};
    passkey::CborReader reader(tooDeep, sizeof(tooDeep));
    assert(!reader.skip());
  }
}

void testPresenceIsOneShot() {
  auto& gate = passkey::presence();
  gate.clear();

  constexpr char rp[] = "example.com";
  assert(!gate.approve());
  assert(gate.request(passkey::PresenceAction::CreateCredential, rp, sizeof(rp) - 1));
  assert(!gate.request(passkey::PresenceAction::GetAssertion, rp, sizeof(rp) - 1));

  const auto pending = gate.snapshot();
  assert(pending.decision == passkey::PresenceDecision::Waiting);
  assert(pending.action == passkey::PresenceAction::CreateCredential);
  assert(std::strcmp(pending.rpId.data(), rp) == 0);

  assert(gate.approve());
  assert(gate.consumeApproval());
  assert(!gate.consumeApproval());
  assert(gate.decision() == passkey::PresenceDecision::Idle);

  // A confirmation that was consumed cannot authorize a later operation.
  assert(gate.request(passkey::PresenceAction::GetAssertion, rp, sizeof(rp) - 1));
  assert(!gate.consumeApproval());
  assert(gate.deny());
  assert(!gate.consumeApproval());
  gate.clear();
}

void testPresenceBounds() {
  auto& gate = passkey::presence();
  gate.clear();

  std::array<char, passkey::kMaxPresenceRpIdBytes + 2> tooLong{};
  tooLong.fill('x');
  assert(!gate.request(passkey::PresenceAction::CreateCredential, tooLong.data(), tooLong.size()));
  assert(!gate.request(passkey::PresenceAction::None, "example.com", 11));
  assert(!gate.request(passkey::PresenceAction::CreateCredential, "", 0));
  assert(gate.decision() == passkey::PresenceDecision::Idle);
}

}  // namespace

int main() {
  testCborRoundTrip();
  testCborRejectsUnboundedOrMalformedInput();
  testPresenceIsOneShot();
  testPresenceBounds();
  return 0;
}
