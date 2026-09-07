#include "TotpActivity.h"

#include <Memory.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#if defined(SIMULATOR)
#include <HalStorage.h>
#else
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#endif

#include "../../activities/util/KeyboardEntryActivity.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;

constexpr int64_t kClockFloor = 1700000000LL;
constexpr fui::ActionId kActionAdd = 521;
constexpr fui::ActionId kActionDelete = 522;
constexpr fui::ActionId kActionKeep = 523;
constexpr fui::ActionId kActionDeleteConfirm = 524;
constexpr fui::ActionId kActionNoticeBack = 525;
constexpr fui::ActionId kActionUnlock = 526;
constexpr fui::ActionId kActionSetPin = 527;

// Authentik-inspired token cards: compact enough to keep several current codes
// visible together, with enough height for a readable account name + code.
constexpr int16_t kTokenRowHeight = 98;
constexpr int16_t kTokenRowGap = 8;
constexpr int16_t kTokenRowStep = kTokenRowHeight + kTokenRowGap;
// chrome() consumes the fixed 76px header and then applies a 36px top inset.
constexpr int16_t kTokenListTop = toybox::kHeaderHeight + toybox::kGutter * 3;

#if defined(SIMULATOR)
constexpr char kSimStorePath[] = "/.crosspoint/totp.bin";
#endif

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = nullptr) {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (rightLabel != nullptr) {
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle centered(const fui::TextStyle& base, const uint8_t maxLines = 1) {
  fui::TextStyle style = base;
  style.align = fui::TextAlign::Center;
  style.maxLines = maxLines;
  return style;
}

void wipeBytes(void* data, const size_t len) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) p[i] = 0;
}

template <typename AccountT>
uint64_t listCounterSignature(const AccountT* accounts, const uint16_t count, const uint64_t now) {
  uint64_t signature = 1469598103934665603ULL;
  for (uint16_t i = 0; i < count; ++i) {
    const auto& account = accounts[i];
    const uint64_t counter = account.period == 0 ? 0 : now / account.period;
    signature ^= counter + (static_cast<uint64_t>(i) << 32);
    signature *= 1099511628211ULL;
  }
  return signature;
}

}  // namespace

std::unique_ptr<Activity> TotpActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TotpActivity>(renderer, mappedInput);
}

bool TotpActivity::validateStore() {
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
  LOG_DBG("TOTP", "readVault: open NVS");
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", true)) {
    LOG_ERR("TOTP", "readVault: prefs.begin failed");
    return false;
  }
  const size_t bytes = prefs.getBytesLength("vault");
  LOG_DBG("TOTP", "readVault: bytes=%u", static_cast<unsigned>(bytes));
  const bool ok = bytes == sizeof(VaultBlob) && prefs.getBytes("vault", &vault, sizeof(vault)) == sizeof(vault);
  prefs.end();
  return ok && vault.magic == kVaultMagic && vault.version == kVaultVersion;
}

bool TotpActivity::writeEncryptedVault() const {
  if (!unlocked_) return false;

  auto vault = makeUniqueNoThrow<VaultBlob>();
  if (!vault) {
    LOG_ERR("TOTP", "writeVault: no heap for VaultBlob");
    return false;
  }
  vault->salt = vaultSalt_;
  esp_fill_random(vault->iv.data(), vault->iv.size());

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, vaultKey_.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, sizeof(StoreBlob), vault->iv.data(), vault->iv.size(),
                                   nullptr, 0, reinterpret_cast<const unsigned char*>(&store_),
                                   vault->ciphertext.data(), vault->tag.size(), vault->tag.data());
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) {
    LOG_ERR("TOTP", "writeVault: AES-GCM failed rc=%d", rc);
    return false;
  }

  Preferences prefs;
  if (!prefs.begin("crossplay-totp", false)) {
    LOG_ERR("TOTP", "writeVault: prefs.begin failed");
    return false;
  }
  const size_t written = prefs.putBytes("vault", vault.get(), sizeof(VaultBlob));
  prefs.end();
  LOG_DBG("TOTP", "writeVault: wrote=%u", static_cast<unsigned>(written));
  return written == sizeof(VaultBlob);
}

bool TotpActivity::unlockWithPin(const char* pin) {
  auto vault = makeUniqueNoThrow<VaultBlob>();
  auto candidate = makeUniqueNoThrow<StoreBlob>();
  if (!vault || !candidate) {
    LOG_ERR("TOTP", "unlock: no heap for crypto scratch");
    return false;
  }
  if (!readVault(*vault)) return false;

  std::array<uint8_t, kVaultKeyBytes> candidateKey{};
  if (!deriveVaultKey(pin, vault->salt, candidateKey)) return false;

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, candidateKey.data(), 256);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, sizeof(StoreBlob), vault->iv.data(), vault->iv.size(), nullptr, 0,
                                  vault->tag.data(), vault->tag.size(), vault->ciphertext.data(),
                                  reinterpret_cast<unsigned char*>(candidate.get()));
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) {
    wipeBytes(candidateKey.data(), candidateKey.size());
    wipeBytes(candidate.get(), sizeof(StoreBlob));
    return false;
  }

  store_ = *candidate;
  wipeBytes(candidate.get(), sizeof(StoreBlob));
  if (!validateStore()) {
    wipeBytes(candidateKey.data(), candidateKey.size());
    return false;
  }

  vaultKey_ = candidateKey;
  vaultSalt_ = vault->salt;
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
        shownCounter_ = UINT64_MAX;
        shownClockValid_ = false;
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
              shownCounter_ = UINT64_MAX;
              shownClockValid_ = false;
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

void TotpActivity::onEnter() {
  LOG_INF("TOTP", "onEnter: begin");
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
  const bool hasVault = vaultExists();
  LOG_INF("TOTP", "onEnter: vault=%d", hasVault ? 1 : 0);
  phase_ = hasVault ? Phase::Locked : Phase::SetupPin;
#endif
  shownCounter_ = UINT64_MAX;
  shownClockValid_ = false;
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

bool TotpActivity::clockValid() const { return static_cast<int64_t>(std::time(nullptr)) > kClockFloor; }

uint64_t TotpActivity::currentCounter() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(store_.count)) return 0;
  const Account& account = store_.accounts[static_cast<size_t>(selected_)];
  if (!clockValid() || account.period == 0) return 0;
  return static_cast<uint64_t>(std::time(nullptr)) / account.period;
}

void TotpActivity::showNotice(const char* headline, const char* message, const Phase returnTo) {
  std::snprintf(noticeHeadline_, sizeof(noticeHeadline_), "%s", headline == nullptr ? "" : headline);
  std::snprintf(noticeMessage_, sizeof(noticeMessage_), "%s", message == nullptr ? "" : message);
  noticeReturn_ = returnTo;
  phase_ = Phase::Notice;
  requestUpdate();
}

void TotpActivity::beginAdd() {
  if (store_.count >= kMaxAccounts) {
    showNotice("FULL", "This build stores up to 16 TOTP accounts.");
    return;
  }

  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "ACCOUNT NAME", "", 39, InputType::Text);
  if (!keyboard) {
    showNotice("LOW MEMORY", "The account editor could not be opened. Try again after leaving other apps.");
    return;
  }
  startActivityForResult(
      std::move(keyboard),
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        const std::string& name = std::get<KeyboardResult>(result.data).text;
        if (name.empty()) {
          showNotice("NO NAME", "Give the account a name before saving it.");
          return;
        }
        addSecretForName(name.c_str());
      });
}

void TotpActivity::addSecretForName(const char* name) {
  const std::string savedName(name == nullptr ? "" : name);
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "BASE32 SECRET", "",
                                                            totp::kMaxSecretChars, InputType::Password);
  if (!keyboard) {
    showNotice("LOW MEMORY", "The secret editor could not be opened. Try again after leaving other apps.");
    return;
  }
  startActivityForResult(
      std::move(keyboard),
      [this, savedName](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;
        addAccount(savedName.c_str(), std::get<KeyboardResult>(result.data).text.c_str());
      });
}

void TotpActivity::addAccount(const char* name, const char* secretInput) {
  if (store_.count >= kMaxAccounts) return;

  char normalized[totp::kMaxSecretChars + 1]{};
  if (!totp::normalizeSecret(secretInput, normalized, sizeof(normalized))) {
    showNotice("BAD SECRET", "The secret must be Base32: A-Z and 2-7. Spaces and dashes are fine.");
    return;
  }

  Account& account = store_.accounts[store_.count];
  account = Account{};
  std::snprintf(account.name, sizeof(account.name), "%s", name);
  std::snprintf(account.secret, sizeof(account.secret), "%s", normalized);
  account.digits = 6;
  account.period = 30;
  ++store_.count;

  if (!saveAccounts()) {
    --store_.count;
    store_.accounts[store_.count] = Account{};
    showNotice("NOT SAVED", "The account could not be written to internal storage.");
    return;
  }

  selected_ = -1;
  phase_ = Phase::List;
  shownCounter_ = UINT64_MAX;
  shownClockValid_ = false;
  requestUpdate();
}

void TotpActivity::openAccount(const int index) {
  if (index < 0 || index >= static_cast<int>(store_.count)) return;
  selected_ = index;
  phase_ = Phase::Code;
  shownCounter_ = UINT64_MAX;
  shownClockValid_ = false;
  requestUpdate();
}

void TotpActivity::deleteSelected() {
  if (selected_ < 0 || selected_ >= static_cast<int>(store_.count)) return;
  const int remove = selected_;
  for (int i = remove; i + 1 < static_cast<int>(store_.count); ++i) {
    store_.accounts[static_cast<size_t>(i)] = store_.accounts[static_cast<size_t>(i + 1)];
  }
  --store_.count;
  store_.accounts[store_.count] = Account{};
  if (!saveAccounts()) {
    showNotice("NOT SAVED", "The account was removed in memory, but storage did not accept the change.");
    return;
  }
  selected_ = -1;
  if (topIndex_ >= static_cast<int>(store_.count)) topIndex_ = 0;
  phase_ = Phase::List;
  shownCounter_ = UINT64_MAX;
  shownClockValid_ = false;
  requestUpdate();
}

void TotpActivity::pageList(const int delta) {
  if (visibleRows_ <= 0 || store_.count <= static_cast<uint16_t>(visibleRows_)) return;
  const int pages = (static_cast<int>(store_.count) + visibleRows_ - 1) / visibleRows_;
  int page = topIndex_ / visibleRows_;
  page += delta;
  if (page < 0) page = 0;
  if (page >= pages) page = pages - 1;
  const int next = page * visibleRows_;
  if (next != topIndex_) {
    topIndex_ = next;
    requestUpdate();
  }
}

void TotpActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    switch (phase_) {
      case Phase::Locked:
      case Phase::SetupPin:
      case Phase::List:
        shelf::leave(renderer, mappedInput);
        break;
      case Phase::ConfirmDelete:
      case Phase::Code:
        phase_ = Phase::List;
        requestUpdate();
        break;
      case Phase::Notice:
        phase_ = noticeReturn_;
        requestUpdate();
        break;
    }
    return;
  }

  if (phase_ == Phase::List) {
    int holdX = 0;
    int holdY = 0;
    if (mappedInput.wasScreenLongPress(holdX, holdY)) {
      const int offset = holdY - kTokenListTop;
      if (offset >= 0) {
        const int row = offset / kTokenRowStep;
        const int withinRow = offset % kTokenRowStep;
        const int index = topIndex_ + row;
        if (row >= 0 && row < visibleRows_ && withinRow < kTokenRowHeight && index >= 0 &&
            index < static_cast<int>(store_.count)) {
          selected_ = index;
          phase_ = Phase::ConfirmDelete;
          requestUpdate();
        }
      }
      return;
    }

    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
    if (next || swipe == MappedInputManager::SwipeDir::Up) {
      pageList(1);
      return;
    }
    if (prev || swipe == MappedInputManager::SwipeDir::Down) {
      pageList(-1);
      return;
    }

    // Empty vaults are static; avoid repainting an e-ink screen just because a
    // valid clock became available while there is no token to update.
    if (store_.count > 0) {
      const bool valid = clockValid();
      const uint64_t now = valid ? static_cast<uint64_t>(std::time(nullptr)) : 0;
      const uint64_t signature = valid ? listCounterSignature(store_.accounts.data(), store_.count, now) : 0;
      if (valid != shownClockValid_ || (valid && signature != shownCounter_)) requestUpdate();
    }
  }

  if (phase_ == Phase::Code && selected_ >= 0 && selected_ < static_cast<int>(store_.count)) {
    const bool valid = clockValid();
    const uint64_t counter = valid ? currentCounter() : 0;
    if (valid != shownClockValid_ || (valid && counter != shownCounter_)) requestUpdate();
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case kActionAdd:
      beginAdd();
      break;
    case kActionDelete:
      phase_ = Phase::ConfirmDelete;
      requestUpdate();
      break;
    case kActionKeep:
      phase_ = Phase::List;
      requestUpdate();
      break;
    case kActionDeleteConfirm:
      deleteSelected();
      break;
    case kActionNoticeBack:
      phase_ = noticeReturn_;
      requestUpdate();
      break;
#if !defined(SIMULATOR)
    case kActionUnlock:
      beginUnlock();
      break;
    case kActionSetPin:
      beginSetPin();
      break;
#endif
    default:
      break;
  }
}

void TotpActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Keep every Authenticator view in the normal UI/display cuts. The previous
  // detail screen bound the huge score face into BODY, which also enlarged the
  // account label and status text and produced the clipped layout seen on-device.
  auto target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  interactions_.clear();
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);

  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const fui::Rect safe = frame.safeRect();
  const int16_t footerY = static_cast<int16_t>(safe.y + safe.height - toybox::kMargin - toybox::kPillHeight);

  switch (phase_) {
    case Phase::Locked: {
      chrome(screen, "AUTHENTICATOR");
      const fui::Rect body = screen.body();
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 90), width, 180),
                  "VAULT LOCKED\nEnter your PIN to decrypt the stored TOTP accounts.", centered(screen.theme().bodyText, 4));
      fui::ButtonProps unlock;
      unlock.label = "UNLOCK";
      unlock.action = kActionUnlock;
      unlock.styles = toybox::rowStyles();
      screen.button(unlock, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));
      break;
    }

    case Phase::SetupPin: {
      chrome(screen, "AUTHENTICATOR");
      const fui::Rect body = screen.body();
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 70), width, 230),
                  "SET UP ENCRYPTED VAULT\nCreate a 6 to 12 digit PIN. Secrets are encrypted before they are written to internal storage.",
                  centered(screen.theme().bodyText, 5));
      fui::ButtonProps setup;
      setup.label = "SET PIN";
      setup.action = kActionSetPin;
      setup.styles = toybox::rowStyles();
      screen.button(setup, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));
      break;
    }

    case Phase::List: {
      char countLabel[16];
      std::snprintf(countLabel, sizeof(countLabel), "%u / %d", static_cast<unsigned>(store_.count), kMaxAccounts);
      chrome(screen, "AUTHENTICATOR", countLabel);

      fui::ButtonProps add;
      add.label = store_.count < kMaxAccounts ? "ADD ACCOUNT" : "FULL";
      add.action = kActionAdd;
      add.styles = toybox::rowStyles();
      screen.button(add, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));

      const fui::Rect body = screen.body();
      const int16_t listBottom = static_cast<int16_t>(footerY - toybox::kGutter);
      const int16_t listHeight = static_cast<int16_t>(listBottom - body.y);
      visibleRows_ = listHeight > 0 ? listHeight / kTokenRowStep : 0;
      if (visibleRows_ < 1 && listHeight >= kTokenRowHeight) visibleRows_ = 1;

      if (store_.count == 0) {
        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 70), width, 120),
                    "NO ACCOUNTS\nAdd a TOTP account on the device or in the web interface.",
                    centered(screen.theme().bodyText, 3));
        topIndex_ = 0;
        shownClockValid_ = false;
        shownCounter_ = 0;
      } else {
        if (visibleRows_ > 0) {
          const int maxTop = ((static_cast<int>(store_.count) - 1) / visibleRows_) * visibleRows_;
          if (topIndex_ > maxTop) topIndex_ = maxTop;
        }

        const bool validClock = clockValid();
        const uint64_t now = validClock ? static_cast<uint64_t>(std::time(nullptr)) : 0;
        fui::TextStyle nameStyle = screen.theme().bodyText;
        nameStyle.maxLines = 1;
        fui::TextStyle codeStyle = screen.theme().titleText;
        codeStyle.color = fui::Color::Black;
        codeStyle.align = fui::TextAlign::Left;
        codeStyle.maxLines = 1;

        for (int row = 0; row < visibleRows_; ++row) {
          const int index = topIndex_ + row;
          if (index >= static_cast<int>(store_.count)) break;
          const Account& account = store_.accounts[static_cast<size_t>(index)];
          const int16_t y = static_cast<int16_t>(body.y + row * kTokenRowStep);
          const fui::Rect card = fui::makeRect(body.x, y, body.width, kTokenRowHeight);
          target.stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 8);

          target.text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                    static_cast<int16_t>(card.y + 5),
                                    static_cast<int16_t>(card.width - 2 * toybox::kGutter), 34),
                      account.name, nameStyle);

          char shown[20]{};
          if (validClock) {
            bool ok = false;
            const uint32_t code = totp::generate(account.secret, now, account.digits, account.period, &ok);
            if (ok) {
              char raw[12]{};
              std::snprintf(raw, sizeof(raw), "%0*u", static_cast<int>(account.digits), static_cast<unsigned>(code));
              const int split = account.digits / 2;
              std::snprintf(shown, sizeof(shown), "%.*s %s", split, raw, raw + split);
            } else {
              std::snprintf(shown, sizeof(shown), "INVALID SECRET");
            }
          } else {
            std::snprintf(shown, sizeof(shown), "CLOCK NOT SET");
          }
          target.text(fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                    static_cast<int16_t>(card.y + 34),
                                    static_cast<int16_t>(card.width - 2 * toybox::kGutter), 63),
                      shown, codeStyle);
        }

        shownClockValid_ = validClock;
        shownCounter_ = validClock ? listCounterSignature(store_.accounts.data(), store_.count, now) : 0;
      }
      break;
    }

    case Phase::Code: {
      // Kept only for backwards-compatible state transitions. New navigation
      // stays on the all-token list; a normal row tap no longer opens a detail.
      if (selected_ < 0 || selected_ >= static_cast<int>(store_.count)) {
        phase_ = Phase::List;
        break;
      }
      const Account& account = store_.accounts[static_cast<size_t>(selected_)];
      chrome(screen, "AUTHENTICATOR");
      const fui::Rect body = screen.body();
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + toybox::kGutter), width, 48), account.name,
                  centered(screen.theme().smallText));

      const bool validClock = clockValid();
      if (validClock) {
        bool ok = false;
        const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        const uint32_t code = totp::generate(account.secret, now, account.digits, account.period, &ok);
        if (ok) {
          char raw[12]{};
          char shown[16]{};
          std::snprintf(raw, sizeof(raw), "%0*u", static_cast<int>(account.digits), static_cast<unsigned>(code));
          const int split = account.digits / 2;
          std::snprintf(shown, sizeof(shown), "%.*s %s", split, raw, raw + split);
          fui::TextStyle codeStyle = centered(screen.theme().titleText);
          codeStyle.color = fui::Color::Black;
          target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 100), width, 90), shown, codeStyle);
          shownCounter_ = now / account.period;
          shownClockValid_ = true;
        }
      } else {
        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 120), width, 90), "CLOCK NOT SET",
                    centered(screen.theme().bodyText, 2));
        shownClockValid_ = false;
        shownCounter_ = 0;
      }
      break;
    }

    case Phase::ConfirmDelete: {
      chrome(screen, "DELETE ACCOUNT?");
      const fui::Rect body = screen.body();
      const char* name = selected_ >= 0 && selected_ < static_cast<int>(store_.count)
                             ? store_.accounts[static_cast<size_t>(selected_)].name
                             : "THIS ACCOUNT";
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 80), width, 100), name,
                  centered(screen.theme().bodyText, 2));
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 190), width, 100),
                  "Long press selected this token. Delete its secret from this device?",
                  centered(screen.theme().smallText, 3));

      const int16_t half = static_cast<int16_t>((width - toybox::kGutter) / 2);
      fui::ButtonProps keep;
      keep.label = "KEEP";
      keep.action = kActionKeep;
      keep.styles = toybox::rowStyles();
      screen.button(keep, fui::makeRect(toybox::kMargin, footerY, half, toybox::kPillHeight));

      fui::ButtonProps remove;
      remove.label = "DELETE";
      remove.action = kActionDeleteConfirm;
      remove.styles = toybox::rowStyles();
      screen.button(remove, fui::makeRect(static_cast<int16_t>(toybox::kMargin + half + toybox::kGutter), footerY,
                                          static_cast<int16_t>(width - half - toybox::kGutter), toybox::kPillHeight));
      break;
    }

    case Phase::Notice: {
      chrome(screen, noticeHeadline_);
      const fui::Rect body = screen.body();
      target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 80), width, 220), noticeMessage_,
                  centered(screen.theme().bodyText, 5));
      fui::ButtonProps back;
      back.label = noticeReturn_ == Phase::List ? "BACK TO ACCOUNTS" : "TRY AGAIN";
      back.action = kActionNoticeBack;
      back.styles = toybox::rowStyles();
      screen.button(back, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Authenticator");
  const auto labels = mappedInput.mapLabels("Back", "", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
