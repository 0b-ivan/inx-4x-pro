#include "PasskeyPresence.h"

#include <algorithm>
#include <cstring>

namespace passkey {

bool PasskeyPresence::request(const PresenceAction action, const char* rpId, const std::size_t rpIdLength) {
  if (action == PresenceAction::None || rpId == nullptr || rpIdLength == 0 || rpIdLength > kMaxPresenceRpIdBytes) {
    return false;
  }

  uint8_t expected = static_cast<uint8_t>(PresenceDecision::Idle);
  if (!decision_.compare_exchange_strong(expected, static_cast<uint8_t>(PresenceDecision::Waiting),
                                         std::memory_order_acq_rel)) {
    return false;
  }

  // request() is the sole writer while the state is Waiting. Copy the display
  // data before publishing the action; readers only display it while Waiting.
  std::fill(rpId_.begin(), rpId_.end(), '\0');
  std::memcpy(rpId_.data(), rpId, rpIdLength);
  action_.store(static_cast<uint8_t>(action), std::memory_order_release);
  return true;
}

PresenceSnapshot PasskeyPresence::snapshot() const {
  PresenceSnapshot out;
  out.decision = static_cast<PresenceDecision>(decision_.load(std::memory_order_acquire));
  out.action = static_cast<PresenceAction>(action_.load(std::memory_order_acquire));
  if (out.decision != PresenceDecision::Idle) out.rpId = rpId_;
  return out;
}

bool PasskeyPresence::approve() {
  uint8_t expected = static_cast<uint8_t>(PresenceDecision::Waiting);
  return decision_.compare_exchange_strong(expected, static_cast<uint8_t>(PresenceDecision::Approved),
                                           std::memory_order_acq_rel);
}

bool PasskeyPresence::deny() {
  uint8_t expected = static_cast<uint8_t>(PresenceDecision::Waiting);
  return decision_.compare_exchange_strong(expected, static_cast<uint8_t>(PresenceDecision::Denied),
                                           std::memory_order_acq_rel);
}

PresenceDecision PasskeyPresence::decision() const {
  return static_cast<PresenceDecision>(decision_.load(std::memory_order_acquire));
}

void PasskeyPresence::clear() {
  action_.store(static_cast<uint8_t>(PresenceAction::None), std::memory_order_release);
  std::fill(rpId_.begin(), rpId_.end(), '\0');
  decision_.store(static_cast<uint8_t>(PresenceDecision::Idle), std::memory_order_release);
}

PasskeyPresence& presence() {
  static PasskeyPresence instance;
  return instance;
}

}  // namespace passkey
