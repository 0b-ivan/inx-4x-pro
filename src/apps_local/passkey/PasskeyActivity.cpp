#include "PasskeyActivity.h"

#include <GfxRenderer.h>
#include <Memory.h>

#include <cstdio>

#include "../Shelf.h"
#if !defined(SIMULATOR)
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#endif
#include "PasskeyPresence.h"
#include "PasskeyStore.h"
#include "PasskeyUsb.h"
#include "fontIds.h"

#if !defined(SIMULATOR)
namespace {
namespace fui = freeink::ui;

fui::TextStyle centered(const fui::TextStyle& base, const uint8_t maxLines = 1) {
  fui::TextStyle style = base;
  style.align = fui::TextAlign::Center;
  style.maxLines = maxLines;
  return style;
}

void chrome(toybox::Screen& screen) {
  fui::HeaderProps header;
  header.title = "USB PASSKEY";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace
#endif

std::unique_ptr<Activity> PasskeyActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PasskeyActivity>(renderer, mappedInput);
}

void PasskeyActivity::onEnter() {
  Activity::onEnter();
#if !defined(SIMULATOR)
  toybox::ensureFonts(renderer);
#endif
  usbStarted_ = passkey::usbPasskey().begin();
  shownReady_ = passkey::usbPasskey().ready();
  shownPackets_ = passkey::usbPasskey().packetsSeen();

  auto& store = passkey::credentialStore();
  shownStoreReady_ = store.ready() || store.begin();
  shownCredentials_ = shownStoreReady_ ? static_cast<unsigned>(store.credentialCount()) : 0U;
  shownPresenceState_ = static_cast<uint8_t>(passkey::presence().decision());
  requestUpdate();
}

void PasskeyActivity::loop() {
  passkey::usbPasskey().poll();

  const auto pending = passkey::presence().snapshot();
  if (pending.decision == passkey::PresenceDecision::Waiting) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      passkey::presence().approve();
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      passkey::presence().deny();
      requestUpdate();
      return;
    }

    // Touch users get the same two explicit actions in the bottom band. A tap
    // that happened before the CTAP request cannot reach this branch.
    int col = -1;
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    const int margin = 24;
    const int available = width - margin * 2;
    const int step = available / 2;
    if (mappedInput.colTouch(col, margin, step, 2, height - 120, height - 24, step - 8) ==
        MappedInputManager::RowTouch::Tap) {
      if (col == 0) {
        passkey::presence().deny();
      } else if (col == 1) {
        passkey::presence().approve();
      }
      requestUpdate();
      return;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  const bool ready = passkey::usbPasskey().ready();
  const unsigned long packets = passkey::usbPasskey().packetsSeen();
  const uint8_t presenceState = static_cast<uint8_t>(passkey::presence().decision());
  if (ready != shownReady_ || packets != shownPackets_ || presenceState != shownPresenceState_) {
    shownReady_ = ready;
    shownPackets_ = packets;
    shownPresenceState_ = presenceState;
    auto& store = passkey::credentialStore();
    shownCredentials_ = store.ready() ? static_cast<unsigned>(store.credentialCount()) : 0U;
    requestUpdate();
  }
}

void PasskeyActivity::render(RenderLock&&) {
  renderer.clearScreen();

#if defined(SIMULATOR)
  const int top = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_10_FONT_ID, top,
                            "USB passkey mode is hardware-only. Build x4pro_passkey for the X4 Pro.");
  renderer.displayBuffer();
  return;
#else
  auto target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);
  chrome(screen);

  const fui::Rect body = screen.body();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

#if defined(CROSSPOINT_USB_PASSKEY)
  const auto pending = passkey::presence().snapshot();
  if (pending.decision == passkey::PresenceDecision::Waiting) {
    const char* title = pending.action == passkey::PresenceAction::CreateCredential ? "PASSKEY ERSTELLEN?"
                                                                                   : "ANMELDUNG BESTAETIGEN?";
    target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 30), width, 55), title,
                centered(screen.theme().titleText, 2));
    target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 105), width, 90), pending.rpId.data(),
                centered(screen.theme().bodyText, 3));
    target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 215), width, 70),
                "Nur bestaetigen, wenn du diese Anfrage gerade selbst gestartet hast.",
                centered(screen.theme().smallText, 3));

    const auto labels = mappedInput.mapLabels("Abbrechen", "Bestaetigen", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const char* state = !usbStarted_ ? "USB START FAILED" : (shownReady_ ? "FIDO2 READY" : "WAITING FOR USB HOST");
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 35), width, 55), state,
              centered(screen.theme().titleText, 2));

  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 105), width, 120),
              "FIDO2 / ES256. Oeffne diese App fuer Registrierungen und Logins; jede Operation braucht eine neue physische Bestaetigung.",
              centered(screen.theme().bodyText, 5));

  char storeStats[96];
  std::snprintf(storeStats, sizeof(storeStats), "Credential vault: %s  %u/%u\nRoot key: software NVS (development only)",
                shownStoreReady_ ? "READY" : "FAILED", shownCredentials_,
                static_cast<unsigned>(passkey::kMaxStoredCredentials));
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 260), width, 72), storeStats,
              centered(screen.theme().smallText, 3));

  char stats[48];
  std::snprintf(stats, sizeof(stats), "USB packets: %lu", shownPackets_);
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 345), width, 42), stats,
              centered(screen.theme().smallText, 1));
#else
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 70), width, 180),
              "This firmware was not built in USB passkey mode. Build the x4pro_passkey environment to switch the ESP32-S3 USB PHY to TinyUSB HID.",
              centered(screen.theme().bodyText, 5));
#endif

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
#endif
}
