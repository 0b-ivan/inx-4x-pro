#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "../../activities/Activity.h"
#include "../ui/ToyboxScreen.h"
#include "TotpCore.h"

class TotpActivity final : public Activity {
 public:
  TotpActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Authenticator", renderer, mappedInput) {}
  ~TotpActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase : uint8_t { Locked, SetupPin, List, Code, ConfirmDelete, Notice };

  struct Account {
    char name[40]{};
    char secret[totp::kMaxSecretChars + 1]{};
    uint8_t digits = 6;
    uint8_t reserved = 0;
    uint16_t period = 30;
  };

  static constexpr int kMaxAccounts = 16;
  static constexpr uint32_t kStoreMagic = 0x54505431U;  // "TPT1"
  static constexpr uint16_t kStoreVersion = 1;

  struct StoreBlob {
    uint32_t magic = kStoreMagic;
    uint16_t version = kStoreVersion;
    uint16_t count = 0;
    std::array<Account, kMaxAccounts> accounts{};
  };

#if !defined(SIMULATOR)
  static constexpr uint32_t kVaultMagic = 0x32565054U;  // "TPV2"
  static constexpr uint16_t kVaultVersion = 2;
  static constexpr size_t kVaultSaltBytes = 16;
  static constexpr size_t kVaultIvBytes = 12;
  static constexpr size_t kVaultTagBytes = 16;
  static constexpr size_t kVaultKeyBytes = 32;
  static constexpr unsigned int kPbkdf2Iterations = 120000;

  struct VaultBlob {
    uint32_t magic = kVaultMagic;
    uint16_t version = kVaultVersion;
    uint16_t reserved = 0;
    std::array<uint8_t, kVaultSaltBytes> salt{};
    std::array<uint8_t, kVaultIvBytes> iv{};
    std::array<uint8_t, kVaultTagBytes> tag{};
    std::array<uint8_t, sizeof(StoreBlob)> ciphertext{};
  };
#endif

  bool loadAccounts();
  bool validateStore();
  bool saveAccounts() const;
  void beginAdd();
  void addSecretForName(const char* name);
  void addAccount(const char* name, const char* secretInput);
  void openAccount(int index);
  void showNotice(const char* headline, const char* message, Phase returnTo = Phase::List);
  void deleteSelected();
  void pageList(int delta);
  bool clockValid() const;
  uint64_t currentCounter() const;

#if !defined(SIMULATOR)
  bool vaultExists() const;
  bool readVault(VaultBlob& vault) const;
  bool writeEncryptedVault() const;
  bool unlockWithPin(const char* pin);
  bool initialiseVault(const char* pin);
  bool deriveVaultKey(const char* pin, const std::array<uint8_t, kVaultSaltBytes>& salt,
                      std::array<uint8_t, kVaultKeyBytes>& key) const;
  static bool validPin(const char* pin);
  void beginUnlock();
  void beginSetPin();
#endif

  Phase phase_ = Phase::List;
  Phase noticeReturn_ = Phase::List;
  StoreBlob store_{};
  int selected_ = -1;
  int topIndex_ = 0;
  int visibleRows_ = 0;
  uint64_t shownCounter_ = 0;
  bool shownClockValid_ = false;

#if !defined(SIMULATOR)
  std::array<uint8_t, kVaultKeyBytes> vaultKey_{};
  std::array<uint8_t, kVaultSaltBytes> vaultSalt_{};
  bool unlocked_ = false;
#endif

  char noticeHeadline_[40]{};
  char noticeMessage_[160]{};

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
