#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace passkey {

enum class PresenceAction : uint8_t {
  None = 0,
  CreateCredential,
  GetAssertion,
};

enum class PresenceDecision : uint8_t {
  Idle = 0,
  Waiting,
  Approved,
  Denied,
};

constexpr std::size_t kMaxPresenceRpIdBytes = 128;

struct PresenceSnapshot {
  PresenceAction action = PresenceAction::None;
  PresenceDecision decision = PresenceDecision::Idle;
  std::array<char, kMaxPresenceRpIdBytes + 1> rpId{};
};

// Cross-task, one-shot user-presence gate. A CTAP request publishes a fresh
// challenge here; only an input event that happens while that request is
// Waiting can approve it. consumeApproval() is the only successful path into
// credential creation/signing, so stale button presses cannot authorize a
// future operation.
class PasskeyPresence {
 public:
  bool request(PresenceAction action, const char* rpId, std::size_t rpIdLength);
  PresenceSnapshot snapshot() const;
  bool approve();
  bool deny();
  PresenceDecision decision() const;
  bool consumeApproval();
  void clear();

 private:
  std::atomic<uint8_t> decision_{static_cast<uint8_t>(PresenceDecision::Idle)};
  std::atomic<uint8_t> action_{static_cast<uint8_t>(PresenceAction::None)};
  std::array<char, kMaxPresenceRpIdBytes + 1> rpId_{};
};

PasskeyPresence& presence();

}  // namespace passkey
