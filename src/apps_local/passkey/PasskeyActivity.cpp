#include "PasskeyActivity.h"

#include <Memory.h>

#include <cstdio>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "PasskeyUsb.h"

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

std::unique_ptr<Activity> PasskeyActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PasskeyActivity>(renderer, mappedInput);
}

void PasskeyActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  usbStarted_ = passkey::usbPasskey().begin();
  shownReady_ = passkey::usbPasskey().ready();
  shownPackets_ = passkey::usbPasskey().packetsSeen();
  requestUpdate();
}

void PasskeyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    shelf::leave(renderer, mappedInput);
    return;
  }

  passkey::usbPasskey().poll();
  const bool ready = passkey::usbPasskey().ready();
  const unsigned long packets = passkey::usbPasskey().packetsSeen();
  if (ready != shownReady_ || packets != shownPackets_) {
    shownReady_ = ready;
    shownPackets_ = packets;
    requestUpdate();
  }
}

void PasskeyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto target = toybox::makeTarget(renderer, toybox::toyboxFaces());
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  toybox::Interactions interactions;
  toybox::Frame frame(target, device, noInput, interactions);
  toybox::Screen screen(frame);
  chrome(screen);

  const fui::Rect body = screen.body();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
  const char* state = !usbStarted_ ? "USB START FAILED" : (shownReady_ ? "FIDO HID CONNECTED" : "WAITING FOR USB HOST");
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 35), width, 55), state,
              centered(screen.theme().titleText, 2));

  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 115), width, 170),
              "CTAP-HID is active over native USB. INIT, PING and authenticatorGetInfo are implemented. Credential creation and signing stay disabled until the secure store + confirmation path are ready.",
              centered(screen.theme().bodyText, 6));

  char stats[48];
  std::snprintf(stats, sizeof(stats), "USB packets: %lu", shownPackets_);
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 315), width, 42), stats,
              centered(screen.theme().smallText, 1));
#else
  target.text(fui::makeRect(toybox::kMargin, static_cast<int16_t>(body.y + 70), width, 180),
              "This firmware was not built in USB passkey mode. Build the x4pro_passkey environment to switch the ESP32-S3 USB PHY to TinyUSB HID.",
              centered(screen.theme().bodyText, 5));
#endif

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  toybox::drawButtonHints(target, screen.theme(), labels);
  renderer.displayBuffer();
}
