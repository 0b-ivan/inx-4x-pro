#include "PlayerAuth.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace player {

bool PlayerAuth::validPin(const char* pin) {
  if (pin == nullptr) return false;
  for (size_t i = 0; i < 4; ++i) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }
  return pin[4] == '\0';
}

bool PlayerAuth::allZero(const std::array<uint8_t, kPinSaltSize>& value) {
  uint8_t combined = 0;
  for (const uint8_t byte : value) combined |= byte;
  return combined == 0;
}

PinHashResult PlayerAuth::derive(const char* pin, PinCredential& credential) {
  if (!validPin(pin) || !credential.supported()) return PinHashResult::InvalidPin;

  const int result = wc_PBKDF2(
      credential.hash.data(), reinterpret_cast<const byte*>(pin), 4,
      credential.salt.data(), static_cast<int>(credential.salt.size()),
      kPinPbkdf2IterationsV1, static_cast<int>(credential.hash.size()), WC_SHA256);
  return result == 0 ? PinHashResult::Ok : PinHashResult::CryptoError;
}

PinHashResult PlayerAuth::createCredential(const char* pin, PinCredential& out) {
  if (!validPin(pin)) return PinHashResult::InvalidPin;
  if (randomFill_ == nullptr) return PinHashResult::RandomUnavailable;

  for (int attempt = 0; attempt < 4; ++attempt) {
    PinCredential candidate{};
    if (!randomFill_(randomContext_, candidate.salt.data(), candidate.salt.size())) {
      return PinHashResult::RandomUnavailable;
    }
    if (allZero(candidate.salt)) continue;

    const PinHashResult result = derive(pin, candidate);
    if (result != PinHashResult::Ok) return result;
    out = candidate;
    return PinHashResult::Ok;
  }

  return PinHashResult::RandomUnavailable;
}

bool PlayerAuth::constantTimeEqual(const std::array<uint8_t, kPinHashSize>& lhs,
                                   const std::array<uint8_t, kPinHashSize>& rhs) {
  uint8_t difference = 0;
  for (size_t i = 0; i < lhs.size(); ++i) difference |= static_cast<uint8_t>(lhs[i] ^ rhs[i]);
  return difference == 0;
}

PlayerAuth::FailureSlot* PlayerAuth::findFailureSlot(const PlayerId& playerId) {
  for (FailureSlot& slot : failures_) {
    if (!slot.playerId.empty() && slot.playerId == playerId) return &slot;
  }
  return nullptr;
}

PlayerAuth::FailureSlot& PlayerAuth::slotForFailure(const PlayerId& playerId) {
  if (FailureSlot* existing = findFailureSlot(playerId)) {
    existing->touched = ++touchCounter_;
    return *existing;
  }

  FailureSlot* selected = &failures_[0];
  for (FailureSlot& slot : failures_) {
    if (slot.playerId.empty()) {
      selected = &slot;
      break;
    }
    if (slot.touched < selected->touched) selected = &slot;
  }

  *selected = FailureSlot{};
  selected->playerId = playerId;
  selected->touched = ++touchCounter_;
  return *selected;
}

void PlayerAuth::clearFailures(const PlayerId& playerId) {
  if (FailureSlot* slot = findFailureSlot(playerId)) *slot = FailureSlot{};
}

AuthResult PlayerAuth::authenticate(const PlayerId& playerId, const char* pin) {
  if (playerId.empty() || !validPin(pin)) return AuthResult::InvalidArgument;

  if (FailureSlot* slot = findFailureSlot(playerId); slot != nullptr && slot->locked) {
    slot->touched = ++touchCounter_;
    return AuthResult::Locked;
  }

  PinCredential stored{};
  const StoreResult storedResult = store_.getPinCredential(playerId, stored);
  if (storedResult == StoreResult::NotFound) return AuthResult::PlayerNotFound;
  if (storedResult == StoreResult::CredentialMissing) return AuthResult::CredentialMissing;
  if (storedResult != StoreResult::Ok) return AuthResult::StorageError;

  PinCredential candidate{};
  candidate.version = stored.version;
  candidate.salt = stored.salt;
  const PinHashResult derived = derive(pin, candidate);
  if (derived != PinHashResult::Ok) return AuthResult::CryptoError;

  if (constantTimeEqual(stored.hash, candidate.hash)) {
    clearFailures(playerId);
    return AuthResult::Success;
  }

  FailureSlot& slot = slotForFailure(playerId);
  if (slot.failures < kMaxFailedAttempts) ++slot.failures;
  if (slot.failures >= kMaxFailedAttempts) {
    slot.locked = true;
    return AuthResult::Locked;
  }
  return AuthResult::WrongPin;
}

}  // namespace player
