#include "PlayerHubScreen.h"

#include <cstdio>

#include "PlayerAvatar.h"

namespace playerhubui {

void buildPlayerHub(toybox::Screen& screen, const Model& model) {
  namespace fui = freeink::ui;

  fui::HeaderProps header;
  header.title = "PLAYERS";
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  toybox::headerRule(screen);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});

  // Two wide rows are intentionally used instead of four narrow footer buttons.
  // On the 480px panel four labels were clipped to GUE... / CAL... / PRO... / REGI...
  // and were hard to hit. Keep the actions compact, but never sacrifice legibility.
  constexpr int16_t kActionGap = 6;
  const int16_t actionRowHeight = toybox::kRowHeight;
  const fui::Rect actions = screen.takeBottom(static_cast<int16_t>(actionRowHeight * 2 + kActionGap), toybox::kGutter);
  const int16_t actionWidth = static_cast<int16_t>((actions.width - kActionGap) / 2);
  const fui::Rect guestButton = fui::makeRect(actions.x, actions.y, actionWidth, actionRowHeight);
  const fui::Rect profileButton = fui::makeRect(static_cast<int16_t>(guestButton.right() + kActionGap), actions.y,
                                                static_cast<int16_t>(actions.right() - guestButton.right() - kActionGap),
                                                actionRowHeight);
  const fui::Rect callsignButton = fui::makeRect(actions.x, static_cast<int16_t>(actions.y + actionRowHeight + kActionGap),
                                                 actionWidth, actionRowHeight);
  const fui::Rect registerButton =
      fui::makeRect(static_cast<int16_t>(callsignButton.right() + kActionGap), callsignButton.y,
                    static_cast<int16_t>(actions.right() - callsignButton.right() - kActionGap), actionRowHeight);

  fui::ButtonProps guest;
  guest.label = "GUEST";
  guest.action = ActionUseGuest;
  guest.enabled = model.guestAvailable && !model.guestSelected;
  if (!guest.enabled) guest.styles = toybox::disabledButtonStyles();
  screen.button(guest, guestButton);

  fui::ButtonProps profile;
  profile.label = "PROFILE";
  profile.action = ActionViewProfile;
  profile.enabled = model.profileAvailable;
  if (!profile.enabled) profile.styles = toybox::disabledButtonStyles();
  screen.button(profile, profileButton);

  fui::ButtonProps callsign;
  callsign.label = "CALLSIGN";
  callsign.action = ActionEditCallsign;
  callsign.enabled = model.guestAvailable && model.guestSelected;
  if (!callsign.enabled) callsign.styles = toybox::disabledButtonStyles();
  screen.button(callsign, callsignButton);

  fui::ButtonProps registration;
  registration.label = "REGISTER";
  registration.action = ActionRegisterGuest;
  registration.enabled = model.guestAvailable && model.guestSelected && model.canRegisterGuest;
  if (!registration.enabled) registration.styles = toybox::disabledButtonStyles();
  screen.button(registration, registerButton);

  // Current local identity. Keep it deliberately compact; detailed progression
  // belongs on PROFILE, not on the login/player picker.
  const fui::Rect card = screen.takeTop(86, toybox::kGutter);
  screen.target().stroke(card, fui::Paint::solid(fui::Color::Black), toybox::kHairline, 10);

  constexpr int16_t kFace = 46;
  const fui::Rect face = fui::makeRect(static_cast<int16_t>(card.x + toybox::kGutter),
                                       static_cast<int16_t>(card.y + (card.height - kFace) / 2), kFace, kFace);
  if (model.currentCallsign != nullptr && model.currentCallsign[0] != '\0') {
    player::drawAvatar(screen.target(), face, model.currentCallsign, player::AvatarSize::Row);
  }

  const int16_t textX = static_cast<int16_t>(face.right() + toybox::kGutter);
  const fui::Rect textBand = fui::makeRect(textX, card.y,
                                           static_cast<int16_t>(card.right() - toybox::kGutter - textX), card.height);

  fui::TextStyle nameStyle;
  nameStyle.font = toybox::kUiFont;
  nameStyle.align = fui::TextAlign::Left;
  nameStyle.color = fui::Color::Black;
  screen.target().text(fui::makeRect(textBand.x, static_cast<int16_t>(textBand.y + 4), textBand.width, 30),
                       model.currentName, nameStyle);

  fui::TextStyle smallStyle;
  smallStyle.font = toybox::kSmallFont;
  smallStyle.align = fui::TextAlign::Left;
  smallStyle.color = fui::Color::Black;
  screen.target().text(fui::makeRect(textBand.x, static_cast<int16_t>(textBand.y + 34), textBand.width, 20),
                       model.currentCallsign, smallStyle);

  char fallback[48]{};
  const char* status = model.message;
  if (status == nullptr || status[0] == '\0') {
    if (!model.guestAvailable && model.activePlayerIndex < 0) {
      status = "PLAYER STORAGE UNAVAILABLE";
    } else if (model.guestSelected) {
      if (model.canRegisterGuest) {
        std::snprintf(fallback, sizeof(fallback), "%u MATCH%s - READY TO REGISTER",
                      static_cast<unsigned>(model.guestCompletedMatches),
                      model.guestCompletedMatches == 1 ? "" : "ES");
      } else {
        std::snprintf(fallback, sizeof(fallback), "PLAY ONE MATCH TO REGISTER");
      }
      status = fallback;
    } else {
      status = "REGISTERED PLAYER";
    }
  }
  screen.target().text(fui::makeRect(textBand.x, static_cast<int16_t>(textBand.y + 57), textBand.width, 20), status,
                       smallStyle);

  if (model.playerCount > 0 && model.players != nullptr) {
    fui::ListProps list;
    list.items = model.players;
    list.count = static_cast<uint16_t>(model.playerCount);
    list.topIndex = 0;
    list.selectedIndex = static_cast<int16_t>(model.activePlayerIndex);
    list.action = ActionLoginPlayer;
    list.rowHeight = 52;
    list.rowGap = 4;
    list.centerSingleLine = true;
    screen.list(list);
  } else {
    // Empty state used to consume the whole remaining screen with giant text.
    // A quiet hint directly below the identity leaves the screen feeling like a
    // login/picker instead of an error page.
    const fui::Rect hint = screen.takeTop(42);
    fui::TextStyle emptyStyle = smallStyle;
    emptyStyle.align = fui::TextAlign::Center;
    screen.target().text(hint, "NO SAVED PLAYERS YET", emptyStyle);
  }
}

}  // namespace playerhubui
