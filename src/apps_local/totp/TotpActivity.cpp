#include "TotpActivity.h"

#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#if defined(SIMULATOR)
#include <HalStorage.h>
#else
#include <Preferences.h>
#endif

#include "../../activities/util/KeyboardEntryActivity.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace {

namespace fui = freeink::ui;

constexpr int64_t kClockFloor = 1700000000LL;
constexpr fui::ActionId kActionOpen = 520;
constexpr fui::ActionId kActionAdd = 521;
constexpr fui::ActionId kActionDelete = 522;
constexpr fui::ActionId kActionKeep = 523;
constexpr fui::ActionId kActionDeleteConfirm = 524;
constexpr fui::ActionId kActionNoticeBack = 525;

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

}  // namespace

std::unique_ptr<Activity> TotpActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TotpActivity>(renderer, mappedInput);
}

bool TotpActivity::loadAccounts() {
  store_ = StoreBlob{};
#if defined(SIMULATOR)
  if (!Storage.exists(kSimStorePath)) return true;
  HalFile file = Storage.open(kSimStorePath, O_RDONLY);
  if (!file || file.size() != sizeof(StoreBlob)) return false;
  if (file.read(&store_, sizeof(store_)) != static_cast<int>(sizeof(store_))) return false;
#else
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", true)) return false;
  const size_t bytes = prefs.getBytesLength("accounts");
  if (bytes == 0) {
    prefs.end();
    return true;
  }
  if (bytes != sizeof(StoreBlob) || prefs.getBytes("accounts", &store_, sizeof(store_)) != sizeof(store_)) {
    prefs.end();
    store_ = StoreBlob{};
    return false;
  }
  prefs.end();
#endif

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
  }
  return true;
}

bool TotpActivity::saveAccounts() const {
#if defined(SIMULATOR)
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  HalFile file = Storage.open(kSimStorePath, O_WRITE | O_CREAT | O_TRUNC);
  if (!file) return false;
  const size_t written = file.write(&store_, sizeof(store_));
  file.flush();
  return written == sizeof(store_);
#else
  Preferences prefs;
  if (!prefs.begin("crossplay-totp", false)) return false;
  const size_t written = prefs.putBytes("accounts", &store_, sizeof(store_));
  prefs.end();
  return written == sizeof(store_);
#endif
}

void TotpActivity::onEnter() {
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

bool TotpActivity::clockValid() const { return static_cast<int64_t>(std::time(nullptr)) > kClockFloor; }

uint64_t TotpActivity::currentCounter() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(store_.count)) return 0;
  const Account& account = store_.accounts[static_cast<size_t>(selected_)];
  if (!clockValid() || account.period == 0) return 0;
  return static_cast<uint64_t>(std::time(nullptr)) / account.period;
}

void TotpActivity::showNotice(const char* headline, const char* message) {
  std::snprintf(noticeHeadline_, sizeof(noticeHeadline_), "%s", headline == nullptr ? "" : headline);
  std::snprintf(noticeMessage_, sizeof(noticeMessage_), "%s", message == nullptr ? "" : message);
  phase_ = Phase::Notice;
  requestUpdate();
}

void TotpActivity::beginAdd() {
  if (store_.count >= kMaxAccounts) {
    showNotice("FULL", "This build stores up to 16 TOTP accounts.");
    return;
  }

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "ACCOUNT NAME", "", 39, InputType::Text),
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
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "BASE32 SECRET", "", totp::kMaxSecretChars,
                                              InputType::Password),
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

  selected_ = static_cast<int>(store_.count) - 1;
  phase_ = Phase::Code;
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
    // The in-memory list is still usable for this session, but do not pretend
    // the deletion is durable when NVS/SD refused the write.
    showNotice("NOT SAVED", "The account was removed in memory, but storage did not accept the change.");
    return;
  }
  selected_ = -1;
  if (topIndex_ >= static_cast<int>(store_.count)) topIndex_ = 0;
  phase_ = Phase::List;
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
      case Phase::List:
        shelf::leave(renderer, mappedInput);
        break;
      case Phase::ConfirmDelete:
        phase_ = Phase::Code;
        requestUpdate();
        break;
      case Phase::Code:
      case Phase::Notice:
        phase_ = Phase::List;
        requestUpdate();
        break;
    }
    return;
  }

  if (phase_ == Phase::List) {
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
  }

  if (phase_ == Phase::Code && selected_ >= 0 && selected_ < static_cast<int>(store_.count)) {
    const bool valid = clockValid();
    const uint64_t counter = valid ? currentCounter() : 0;
    if (valid != shownClockValid_ || (valid && counter != shownCounter_)) {
      requestUpdate();
    }
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
    case kActionOpen:
      openAccount(event.value);
      break;
    case kActionAdd:
      beginAdd();
      break;
    case kActionDelete:
      phase_ = Phase::ConfirmDelete;
      requestUpdate();
      break;
    case kActionKeep:
      phase_ = Phase::Code;
      requestUpdate();
      break;
    case kActionDeleteConfirm:
      deleteSelected();
      break;
    case kActionNoticeBack:
      phase_ = Phase::List;
      requestUpdate();
      break;
    default:
      break;
  }
}

void TotpActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const toybox::Faces faces = phase_ == Phase::Code ? toybox::bigNumberFaces() : toybox::toyboxFaces();
  auto target = toybox::makeTarget(renderer, faces);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  interactions_.clear();
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);

  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight);

  switch (phase_) {
    case Phase::List: {
      char countLabel[16];
      std::snprintf(countLabel, sizeof(countLabel), "%u / %d", static_cast<unsigned>(store_.count), kMaxAccounts);
      chrome(screen, "AUTHENTICATOR", countLabel);

      fui::ButtonProps add;
      add.label = store_.count < kMaxAccounts ? "ADD ACCOUNT" : "FULL";
      add.action = store_.count < kMaxAccounts ? kActionAdd : fui::ActionNone;
      add.styles = toybox::rowStyles();
      screen.button(add, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));

      const fui::Rect body = screen.body();
      const int16_t listHeight = static_cast<int16_t>(footerY - toybox::kGutter - body.y);
      if (store_.count == 0) {
        const int16_t line = target.lineHeight(screen.theme().bodyText.font);
        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + toybox::kMargin * 2), width,
                                  static_cast<int16_t>(line * 3)),
                    "NO ACCOUNTS\nTap ADD ACCOUNT to store a TOTP secret.", centered(screen.theme().bodyText, 3));
        visibleRows_ = 0;
        topIndex_ = 0;
      } else {
        std::array<fui::ListItem, kMaxAccounts> rows{};
        std::array<char, 16> values{};
        (void)values;
        for (uint16_t i = 0; i < store_.count; ++i) {
          rows[i].label = store_.accounts[i].name;
          rows[i].value = store_.accounts[i].digits == 8 ? "8 DIGIT" : "6 DIGIT";
          rows[i].actionValue = static_cast<int16_t>(i);
        }

        fui::ListProps list;
        list.items = rows.data();
        list.count = store_.count;
        list.topIndex = static_cast<uint16_t>(topIndex_);
        list.selectedIndex = -1;
        list.action = kActionOpen;
        list.rowHeight = screen.theme().rowHeight;
        list.labelText = screen.theme().bodyText;
        list.valueText = screen.theme().smallText;
        list.balanceWrappedLabelWithValue = false;

        visibleRows_ = fui::listVisibleRows(fui::makeRect(body.x, body.y, body.width, listHeight), list.rowHeight,
                                            screen.theme().listRowGap);
        if (visibleRows_ > 0) {
          const int maxTop = ((static_cast<int>(store_.count) - 1) / visibleRows_) * visibleRows_;
          if (topIndex_ > maxTop) topIndex_ = maxTop;
        }
        screen.list(list, listHeight, fui::LayoutAnchor::Top);
      }
      break;
    }

    case Phase::Code: {
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
          char raw[12];
          std::snprintf(raw, sizeof(raw), "%0*u", static_cast<int>(account.digits), static_cast<unsigned>(code));
          char shown[16]{};
          const int split = account.digits / 2;
          std::snprintf(shown, sizeof(shown), "%.*s %s", split, raw, raw + split);
          fui::TextStyle codeStyle = centered(screen.theme().bodyText);
          target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 90), width, 150), shown, codeStyle);

          char periodLine[48];
          std::snprintf(periodLine, sizeof(periodLine), "CHANGES EVERY %u SECONDS", static_cast<unsigned>(account.period));
          target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 250), width, 50), periodLine,
                      centered(screen.theme().smallText));
          shownCounter_ = now / account.period;
          shownClockValid_ = true;
        }
      } else {
        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 100), width, 90), "CLOCK NOT SET",
                    centered(screen.theme().smallText, 2));
        target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 200), width, 120),
                    "Set or sync the device clock before using TOTP.", centered(screen.theme().smallText, 3));
        shownClockValid_ = false;
        shownCounter_ = 0;
      }

      fui::ButtonProps remove;
      remove.label = "DELETE ACCOUNT";
      remove.action = kActionDelete;
      remove.styles = toybox::rowStyles();
      screen.button(remove, fui::makeRect(toybox::kMargin, footerY, width, toybox::kPillHeight));
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
                  "The secret will be removed from this device.", centered(screen.theme().smallText, 3));

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
      back.label = "BACK TO ACCOUNTS";
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
