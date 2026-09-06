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
  enum class Phase : uint8_t { List, Code, ConfirmDelete, Notice };

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

  bool loadAccounts();
  bool saveAccounts() const;
  void beginAdd();
  void addSecretForName(const char* name);
  void addAccount(const char* name, const char* secretInput);
  void openAccount(int index);
  void showNotice(const char* headline, const char* message);
  void deleteSelected();
  void pageList(int delta);
  bool clockValid() const;
  uint64_t currentCounter() const;

  Phase phase_ = Phase::List;
  StoreBlob store_{};
  int selected_ = -1;
  int topIndex_ = 0;
  int visibleRows_ = 0;
  uint64_t shownCounter_ = 0;
  bool shownClockValid_ = false;

  char noticeHeadline_[40]{};
  char noticeMessage_[160]{};

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
