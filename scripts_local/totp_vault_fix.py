from pathlib import Path

header = r'''#pragma once

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
'''
Path('src/apps_local/totp/TotpActivity.h').write_text(header)

p = Path('src/apps_local/totp/TotpActivity.cpp')
s = p.read_text()

s = s.replace('''#if defined(SIMULATOR)\n#include <HalStorage.h>\n#else\n#include <Preferences.h>\n#endif\n''', '''#if defined(SIMULATOR)\n#include <HalStorage.h>\n#else\n#include <Preferences.h>\n#include <esp_random.h>\n#include <mbedtls/gcm.h>\n#include <mbedtls/md.h>\n#include <mbedtls/pkcs5.h>\n#endif\n''', 1)

s = s.replace('''constexpr fui::ActionId kActionNoticeBack = 525;\n''', '''constexpr fui::ActionId kActionNoticeBack = 525;\nconstexpr fui::ActionId kActionUnlock = 526;\nconstexpr fui::ActionId kActionSetPin = 527;\n''', 1)

anchor = '''fui::TextStyle centered(const fui::TextStyle& base, const uint8_t maxLines = 1) {\n  fui::TextStyle style = base;\n  style.align = fui::TextAlign::Center;\n  style.maxLines = maxLines;\n  return style;\n}\n'''
replacement = anchor + '''\nvoid wipeBytes(void* data, const size_t len) {\n  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);\n  for (size_t i = 0; i < len; ++i) p[i] = 0;\n}\n'''
if anchor not in s:
    raise SystemExit('centered anchor not found')
s = s.replace(anchor, replacement, 1)

start = s.index('bool TotpActivity::loadAccounts() {')
end = s.index('void TotpActivity::onEnter() {')
storage = r'''bool TotpActivity::validateStore() {
  if (store_.magic != kStoreMagic || store_.version != kStoreVersion || store_.count > kMaxAccounts) {
    store_ = StoreBlob{};
    return false;
  }

  for (uint16_t i = 0; i < store_.count; ++i) {
    Account& account = store_.accounts[i];
    account.name[sizeof(account.name) - 1] = '\0';
    account.secret[sizeof(account.secret) - 1] = '\0';
    if (account.name[0] == '\0' || account.period == 0 || (account.digits != 6 && account.digits != 8)) {
      store_ = StoreBlob{};
      return false;
    }
    char normalized[totp::kMaxSecretChars + 1]{};
    if (!totp::normalizeSecret(account.secret, normalized, sizeof(normalized))) {
      store_ = StoreBlob{};
      return false;
    }
    std::snprintf(account.secret, sizeof(account.secret), "%s", normalized);
  }
  return true;
}

bool TotpActivity::loadAccounts() {
#if defined(SIMULATOR)
  store_ = StoreBlob{};
  if (!Storage.exists(kSimStorePath)) return true;
  HalFile file = Storage.open(kSimStorePath, O_RDONLY);
  if (!file || file.size() != sizeof(StoreBlob)) return false;
  if (file.read(&store_, sizeof(store_)) != static_cast<int>(sizeof(store_))) return false;
  return validateStore();
#else
  return false;
#endif
}

#if !defined(SIMULATOR)
bool TotpActivity::validPin(const char* pin) {
  if (pin == nullptr) return false;
  const size_t len = std::strlen(pin);
  if (len < 6 || len > 12) return false;
  for (size_t i = 0; i < len; ++i) {
    if (pin[i] < '0' || pin[i] > '9') return false;
  }
  return true;
}

bool TotpActivity::deriveVaultKey(const char* pin, const std::array<uint8_t, kVaultSaltBytes>& salt,
                                  std::array<uint8_t, kVaultKeyBytes>& key) const {
  if (!validPin(pin)) return false;
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(pin),
                                       std::strlen(pin), salt.data(), salt.size(), kPbkdf2Iterations,
                                       static_cast<uint32_t>(key.size()), key.data()) == 0;
}

bool TotpActivity::vaultExists() const {
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", true)) return false;
  const size_t bytes = prefs.getBytesLength("vault");
  prefs.end();
  return bytes > 0;
}

bool TotpActivity::readVault(VaultBlob& vault) const {
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", true)) return false;
  const size_t bytes = prefs.getBytesLength("vault");
  const bool ok = bytes == sizeof(VaultBlob) && prefs.getBytes("vault", &vault, sizeof(vault)) == sizeof(vault);
  prefs.end();
  return ok && vault.magic == kVaultMagic && vault.version == kVaultVersion;
}

bool TotpActivity::writeEncryptedVault() const {
  if (!unlocked_) return false;

  VaultBlob vault{};
  vault.salt = vaultSalt_;
  esp_fill_random(vault.iv.data(), vault.iv.size());

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey_.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(StoreBlob), vault.iv.data(), vault.iv.size(),
                                   nullptr, 0, reinterpret_cast<const unsigned char*>(&store_),
                                   vault.ciphertext.data(), vault.tag.size(), vault.tag.data());
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) return false;

  Preferences prefs;
  if (!prefs.begin("crossplay-totp", false)) return false;
  const size_t written = prefs.putBytes("vault", &vault, sizeof(vault));
  prefs.end();
  return written == sizeof(vault);
}

bool TotpActivity::unlockWithPin(const char* pin) {
  VaultBlob vault{};
  if (!readVault(vault)) return false;

  std::array<uint8_t, kVaultKeyBytes> candidateKey{};
  if (!deriveVaultKey(pin, vault.salt, candidateKey)) return false;

  StoreBlob candidate{};
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, candidateKey.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, sizeof(StoreBlob), vault.iv.data(), vault.iv.size(), nullptr, 0,
                                  vault.tag.data(), vault.tag.size(), vault.ciphertext.data(),
                                  reinterpret_cast<unsigned char*>(&candidate));
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) {
    wipeBytes(candidateKey.data(), candidateKey.size());
    wipeBytes(&candidate, sizeof(candidate));
    return false;
  }

  store_ = candidate;
  wipeBytes(&candidate, sizeof(candidate));
  if (!validateStore()) {
    wipeBytes(candidateKey.data(), candidateKey.size());
    return false;
  }

  vaultKey_ = candidateKey;
  vaultSalt_ = vault.salt;
  wipeBytes(candidateKey.data(), candidateKey.size());
  unlocked_ = true;
  return true;
}

bool TotpActivity::initialiseVault(const char* pin) {
  if (!validPin(pin)) return false;
  store_ = StoreBlob{};
  esp_fill_random(vaultSalt_.data(), vaultSalt_.size());
  if (!deriveVaultKey(pin, vaultSalt_, vaultKey_)) return false;
  unlocked_ = true;
  if (!writeEncryptedVault()) {
    unlocked_ = false;
    wipeBytes(vaultKey_.data(), vaultKey_.size());
    wipeBytes(vaultSalt_.data(), vaultSalt_.size());
    return false;
  }

  Preferences prefs;
  if (prefs.begin("crossplay-totp", false)) {
    prefs.remove("accounts");
    prefs.end();
  }
  return true;
}

void TotpActivity::beginUnlock() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "VAULT PIN", "", 12, InputType::Password);
  if (!keyboard) {
    showNotice("LOW MEMORY", "The PIN keyboard could not be opened. Try again.", Phase::Locked);
    return;
  }
  startActivityForResult(
      std::move(keyboard),
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        const std::string& pin = std::get<KeyboardResult>(result.data).text;
        if (!validPin(pin.c_str())) {
          showNotice("INVALID PIN", "Use 6 to 12 digits.", Phase::Locked);
          return;
        }
        if (!unlockWithPin(pin.c_str())) {
          showNotice("UNLOCK FAILED", "The PIN is wrong or the vault is damaged.", Phase::Locked);
          return;
        }
        phase_ = Phase::List;
        requestUpdate();
      });
}

void TotpActivity::beginSetPin() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "NEW VAULT PIN", "", 12,
                                                            InputType::Password);
  if (!keyboard) {
    showNotice("LOW MEMORY", "The PIN keyboard could not be opened. Try again.", Phase::SetupPin);
    return;
  }
  startActivityForResult(
      std::move(keyboard),
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        const std::string pin = std::get<KeyboardResult>(result.data).text;
        if (!validPin(pin.c_str())) {
          showNotice("INVALID PIN", "Use 6 to 12 digits.", Phase::SetupPin);
          return;
        }

        auto confirm = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "CONFIRM VAULT PIN", "", 12,
                                                                 InputType::Password);
        if (!confirm) {
          showNotice("LOW MEMORY", "The PIN keyboard could not be opened. Try again.", Phase::SetupPin);
          return;
        }
        startActivityForResult(
            std::move(confirm),
            [this, pin](const ActivityResult& confirmResult) {
              if (confirmResult.isCancelled || !std::holds_alternative<KeyboardResult>(confirmResult.data)) return;
              const std::string& confirmation = std::get<KeyboardResult>(confirmResult.data).text;
              if (confirmation != pin) {
                showNotice("PIN MISMATCH", "The two PIN entries did not match.", Phase::SetupPin);
                return;
              }
              if (!initialiseVault(pin.c_str())) {
                showNotice("NOT SAVED", "The encrypted vault could not be created.", Phase::SetupPin);
                return;
              }
              phase_ = Phase::List;
              requestUpdate();
            });
      });
}
#endif

bool TotpActivity::saveAccounts() const {
#if defined(SIMULATOR)
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  HalFile file = Storage.open(kSimStorePath, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) return false;
  const size_t written = file.write(&store_, sizeof(store_));
  file.flush();
  return written == sizeof(store_);
#else
  return writeEncryptedVault();
#endif
}

'''
s = s[:start] + storage + s[end:]

old = r'''void TotpActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  if (!loadAccounts()) {
    showNotice("STORE RESET", "The authenticator store was unreadable and was reset.");
  } else {
    phase_ = Phase::List;
  }
  requestUpdate();
}

void TotpActivity::onExit() {
  Activity::onExit();
  volatile uint8_t* wipe = reinterpret_cast<volatile uint8_t*>(&store_);
  for (size_t i = 0; i < sizeof(store_); ++i) wipe[i] = 0;
}
'''
new = r'''void TotpActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
#if defined(SIMULATOR)
  if (!loadAccounts()) {
    showNotice("STORE RESET", "The authenticator store was unreadable and was reset.");
  } else {
    phase_ = Phase::List;
  }
#else
  store_ = StoreBlob{};
  unlocked_ = false;
  phase_ = vaultExists() ? Phase::Locked : Phase::SetupPin;
#endif
  requestUpdate();
}

void TotpActivity::onExit() {
  Activity::onExit();
  wipeBytes(&store_, sizeof(store_));
#if !defined(SIMULATOR)
  wipeBytes(vaultKey_.data(), vaultKey_.size());
  wipeBytes(vaultSalt_.data(), vaultSalt_.size());
  unlocked_ = false;
#endif
}
'''
if old not in s:
    raise SystemExit('onEnter/onExit anchor not found')
s = s.replace(old, new, 1)

old = '''void TotpActivity::showNotice(const char* headline, const char* message) {\n  std::snprintf(noticeHeadline_, sizeof(noticeHeadline_), "%s", headline == nullptr ? "" : headline);\n  std::snprintf(noticeMessage_, sizeof(noticeMessage_), "%s", message == nullptr ? "" : message);\n  phase_ = Phase::Notice;\n  requestUpdate();\n}\n'''
new = '''void TotpActivity::showNotice(const char* headline, const char* message, const Phase returnTo) {\n  std::snprintf(noticeHeadline_, sizeof(noticeHeadline_), "%s", headline == nullptr ? "" : headline);\n  std::snprintf(noticeMessage_, sizeof(noticeMessage_), "%s", message == nullptr ? "" : message);\n  noticeReturn_ = returnTo;\n  phase_ = Phase::Notice;\n  requestUpdate();\n}\n'''
if old not in s:
    raise SystemExit('showNotice anchor not found')
s = s.replace(old, new, 1)

old = '''    switch (phase_) {\n      case Phase::List:\n        shelf::leave(renderer, mappedInput);\n        break;\n      case Phase::ConfirmDelete:\n        phase_ = Phase::Code;\n        requestUpdate();\n        break;\n      case Phase::Code:\n      case Phase::Notice:\n        phase_ = Phase::List;\n        requestUpdate();\n        break;\n    }\n'''
new = '''    switch (phase_) {\n      case Phase::Locked:\n      case Phase::SetupPin:\n      case Phase::List:\n        shelf::leave(renderer, mappedInput);\n        break;\n      case Phase::ConfirmDelete:\n        phase_ = Phase::Code;\n        requestUpdate();\n        break;\n      case Phase::Code:\n        phase_ = Phase::List;\n        requestUpdate();\n        break;\n      case Phase::Notice:\n        phase_ = noticeReturn_;\n        requestUpdate();\n        break;\n    }\n'''
if old not in s:
    raise SystemExit('back switch anchor not found')
s = s.replace(old, new, 1)

old = '''    case kActionNoticeBack:\n      phase_ = Phase::List;\n      requestUpdate();\n      break;\n    default:\n'''
new = '''    case kActionNoticeBack:\n      phase_ = noticeReturn_;\n      requestUpdate();\n      break;\n#if !defined(SIMULATOR)\n    case kActionUnlock:\n      beginUnlock();\n      break;\n    case kActionSetPin:\n      beginSetPin();\n      break;\n#endif\n    default:\n'''
if old not in s:
    raise SystemExit('action switch anchor not found')
s = s.replace(old, new, 1)

old = '''  switch (phase_) {\n    case Phase::List: {\n'''
new = '''  switch (phase_) {\n    case Phase::Locked: {\n      chrome(screen, "AUTHENTICATOR");\n      const fui::Rect body = screen.body();\n      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 90), width, 180),\n                  "VAULT LOCKED\\nEnter your PIN to decrypt the stored TOTP accounts.", centered(screen.theme().bodyText, 4));\n      fui::ButtonProps unlock;\n      unlock.label = "UNLOCK";\n      unlock.action = kActionUnlock;\n      unlock.styles = toybox::rowStyles();\n      screen.button(unlock, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));\n      break;\n    }\n\n    case Phase::SetupPin: {\n      chrome(screen, "AUTHENTICATOR");\n      const fui::Rect body = screen.body();\n      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 70), width, 230),\n                  "SET UP ENCRYPTED VAULT\\nCreate a 6 to 12 digit PIN. Secrets are encrypted before they are written to internal storage.",\n                  centered(screen.theme().bodyText, 5));\n      fui::ButtonProps setup;\n      setup.label = "SET PIN";\n      setup.action = kActionSetPin;\n      setup.styles = toybox::rowStyles();\n      screen.button(setup, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));\n      break;\n    }\n\n    case Phase::List: {\n'''
if old not in s:
    raise SystemExit('render switch anchor not found')
s = s.replace(old, new, 1)

old = '''      fui::ButtonProps back;\n      back.label = "BACK TO ACCOUNTS";\n      back.action = kActionNoticeBack;\n'''
new = '''      fui::ButtonProps back;\n      back.label = noticeReturn_ == Phase::List ? "BACK TO ACCOUNTS" : "TRY AGAIN";\n      back.action = kActionNoticeBack;\n'''
if old not in s:
    raise SystemExit('notice button anchor not found')
s = s.replace(old, new, 1)

p.write_text(s)
