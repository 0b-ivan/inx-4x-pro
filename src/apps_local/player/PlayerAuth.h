#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "PinCredential.h"
#include "PlayerId.h"
#include "PlayerStore.h"
#include "RandomSource.h"

namespace player {

enum class PinHashResult : uint8_t {
  Ok = 0,
  InvalidPin,
  RandomUnavailable,
  CryptoError,
};

enum class AuthResult : uint8_t {
  Success = 0,
  WrongPin,
  Locked,
  InvalidArgument,
  PlayerNotFound,
  CredentialMissing,
  StorageError,
  CryptoError,
};

class PlayerAuth {
 public:
  static constexpr uint8_t kMaxFailedAttempts = 5;
  static constexpr size_t kFailureSlots = 4;

  PlayerAuth(PlayerStore& store, RandomFill randomFill, void* randomContext = nullptr)
      : store_(store), randomFill_(randomFill), randomContext_(randomContext) {}

  static bool validPin(const char* pin);

  PinHashResult createCredential(const char* pin, PinCredential& out);
  AuthResult authenticate(const PlayerId& playerId, const char* pin);

 private:
  struct FailureSlot {
    PlayerId playerId{};
    uint8_t failures = 0;
    bool locked = false;
    uint32_t touched = 0;
  };

  static PinHashResult derive(const char* pin, PinCredential& credential);
  static bool constantTimeEqual(const std::array<uint8_t, kPinHashSize>& lhs,
                                const std::array<uint8_t, kPinHashSize>& rhs);
  static bool allZero(const std::array<uint8_t, kPinSaltSize>& value);

  FailureSlot* findFailureSlot(const PlayerId& playerId);
  FailureSlot& slotForFailure(const PlayerId& playerId);
  void clearFailures(const PlayerId& playerId);

  PlayerStore& store_;
  RandomFill randomFill_ = nullptr;
  void* randomContext_ = nullptr;
  std::array<FailureSlot, kFailureSlots> failures_{};
  uint32_t touchCounter_ = 0;
};

}  // namespace player
