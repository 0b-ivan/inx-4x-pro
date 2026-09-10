#include "BattleshipScreens.h"

#include <cstdio>

#include "../link/LinkScreens.h"

namespace bshipui {

namespace {

// The header band and its offset rule, as every Toybox screen wears them. A
// fourth local copy rather than a shared helper, for the reason LinkScreens
// gives: a copy is cheaper than a header dependency between apps.
void toyboxChrome(toybox::Screen& screen, const char* title) {
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  toybox::headerRule(screen);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

int startRows(const StartModel& model) {
  return model.hasSavedGame ? static_cast<int>(StartRow::Count) : static_cast<int>(StartRow::Count) - 1;
}

StartRow startRowAt(const StartModel& model, const int visibleIndex) {
  const int count = startRows(model);
  const int clamped = visibleIndex < 0 ? 0 : (visibleIndex >= count ? count - 1 : visibleIndex);
  // With no game to continue, the first row is NEW GAME and everything shifts.
  return static_cast<StartRow>(model.hasSavedGame ? clamped : clamped + 1);
}

const char* startRowLabel(const StartRow row) {
  switch (row) {
    case StartRow::Continue:
      return "CONTINUE";
    case StartRow::NewGame:
      return "NEW GAME";
    case StartRow::PlayNearby:
      // The same words chess uses, and for the same reason: "tap where it says
      // multiplayer" only works if something says it, and NEARBY is what it is.
      return "PLAY NEARBY";
    default:
      return "";
  }
}

fui::Rect buildStartMenu(toybox::Screen& screen, const StartModel& model) {
  toyboxChrome(screen, "BATTLESHIP");

  // Your record, in one line, above a rule. Small: it is worth having but it is
  // not why you opened the app.
  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON   STREAK %d", model.played, model.won, model.streak);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle recordStyle;
  recordStyle.font = toybox::kTileFont;
  recordStyle.align = fui::TextAlign::Left;
  screen.target().text(line, record, recordStyle);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));

  fui::ListItem rows[static_cast<int>(StartRow::Count)] = {};
  const int count = startRows(model);
  for (int i = 0; i < count; ++i) {
    const StartRow row = startRowAt(model, i);
    rows[i].label = startRowLabel(row);
    // Only CONTINUE carries a value, and it is the game you left.
    // No value on CONTINUE. "14 SHOTS, 2 SUNK" beside the word was a label
    // wearing a receipt; the artwork above says how the game stands, in marks.
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionStartRow;
  // Anchored to the bottom margin, so what is left above is one zone for the
  // artwork rather than slack scattered around the rows.
  const int16_t listHeight =
      static_cast<int16_t>(count * toybox::kRowHeight + (count - 1) * toybox::kGutter / 2 + toybox::kGutter);
  // The band the list is about to occupy, taken from the same content rect and
  // height it will use. Needed to put the multiplayer mark on PLAY NEARBY.
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  // One symbol wherever two devices talk to each other, so it is learned once
  // and recognised everywhere. See linkui::nearbyMark().
  for (int i = 0; i < count; ++i) {
    if (startRowAt(model, i) != StartRow::PlayNearby) continue;
    toybox::iconAtRowRight(screen, listBand, i, 0, linkui::nearbyMark(), i == model.selected);
  }

  return screen.body();
}

const char* localPlayRowLabel(const LocalPlayRow row) {
  switch (row) {
    case LocalPlayRow::X4Pro:
      return "X4 PRO";
    case LocalPlayRow::Browser:
      return "BROWSER";
    default:
      return "";
  }
}

fui::Rect buildLocalPlayMenu(toybox::Screen& screen, const LocalPlayModel& model) {
  toyboxChrome(screen, "LOCAL PLAY");

  const fui::Rect intro = screen.takeTop(34);
  fui::TextStyle introStyle;
  introStyle.font = toybox::kTileFont;
  introStyle.align = fui::TextAlign::Left;
  screen.target().text(intro, "CHOOSE YOUR OPPONENT", introStyle);

  fui::ListItem rows[static_cast<int>(LocalPlayRow::Count)] = {};
  for (int i = 0; i < static_cast<int>(LocalPlayRow::Count); ++i) {
    rows[i].label = localPlayRowLabel(static_cast<LocalPlayRow>(i));
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps list;
  list.items = rows;
  list.count = static_cast<uint16_t>(LocalPlayRow::Count);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionLocalPlayRow;

  const int16_t listHeight = static_cast<int16_t>(static_cast<int>(LocalPlayRow::Count) * toybox::kRowHeight +
                                                  (static_cast<int>(LocalPlayRow::Count) - 1) * toybox::kGutter / 2 +
                                                  toybox::kGutter);
  const fui::Rect content = screen.contentRect();
  const fui::Rect listBand =
      fui::makeRect(content.x, static_cast<int16_t>(content.bottom() - listHeight), content.width, listHeight);
  screen.list(list, listHeight, fui::LayoutAnchor::Bottom);

  // The radio mark stays attached to the existing X4-to-X4 transport. Browser
  // deliberately has no second symbol yet: its connection screen will explain
  // Wi-Fi/Hotspot explicitly instead of teaching a new icon without context.
  toybox::iconAtRowRight(screen, listBand, 0, 0, linkui::nearbyMark(), model.selected == 0);

  return screen.body();
}

fui::Rect buildPlaceChrome(toybox::Screen& screen, const PlaceModel& model) {
  // "PLACE YOUR FLEET" came out as "PLACE YOUR FLEE": the display cut is wide
  // and the band does not scroll, so a title has to be short enough to survive
  // it. Shrink to fit is for content; chrome is written to fit.
  toyboxChrome(screen, "YOUR FLEET");

  // Two controls, side by side rather than stacked: stacked pills fuse into one
  // black slab and read as a single control, and this screen has room for
  // neither the height nor the confusion.
  const fui::Rect footer = screen.takeBottom(toybox::kPillHeight);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);

  fui::ButtonProps shuffle;
  shuffle.label = "SHUFFLE";
  shuffle.action = model.canEdit ? static_cast<fui::ActionId>(ActionShuffle) : fui::NO_ACTION;
  shuffle.borderEdges = fui::EdgesNone;
  if (!model.canEdit) shuffle.styles = toybox::disabledButtonStyles();
  screen.button(shuffle, fui::makeRect(footer.x, footer.y, half, footer.height));

  fui::ButtonProps ready;
  ready.label = "READY";
  ready.action = model.canEdit ? static_cast<fui::ActionId>(ActionReady) : fui::NO_ACTION;
  ready.borderEdges = fui::EdgesNone;
  if (!model.canEdit) ready.styles = toybox::disabledButtonStyles();
  screen.button(ready, fui::makeRect(static_cast<int16_t>(footer.right() - half), footer.y, half, footer.height));

  // The instruction sits under the rule rather than over the buttons: it is
  // read once, at the top, and then the eye belongs to the grid.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  fui::TextStyle style;
  style.font = toybox::kTileFont;
  style.align = fui::TextAlign::Left;
  screen.target().text(line, model.status, style);

  return screen.body();
}

fui::Rect buildBoardChrome(toybox::Screen& screen, const BoardModel& model) {
  toyboxChrome(screen, "BATTLESHIP");

  // What happened last remains a separate line. The bottom row is now always
  // explicit about actions: FIRE / SURRENDER while playing, and outcome /
  // PLAY AGAIN once the round has ended.
  const fui::Rect line = screen.takeTop(26, toybox::kGutter / 2);
  fui::TextStyle reportStyle;
  reportStyle.font = toybox::kTileFont;
  reportStyle.align = fui::TextAlign::Left;
  screen.target().text(line, model.report, reportStyle);

  const fui::Rect footer = linkui::withOpponentFace(screen, screen.takeBottom(toybox::kPillHeight), model.theirName);
  const int16_t half = static_cast<int16_t>((footer.width - toybox::kGutter) / 2);
  const fui::Rect left = fui::makeRect(footer.x, footer.y, half, footer.height);
  const fui::Rect right = fui::makeRect(static_cast<int16_t>(footer.right() - half), footer.y, half, footer.height);

  fui::ButtonProps primary;
  primary.label = model.status;
  primary.borderEdges = fui::EdgesNone;

  fui::ButtonProps secondary;
  secondary.borderEdges = fui::EdgesNone;

  if (model.gameOver) {
    primary.action = fui::NO_ACTION;
    secondary.label = "PLAY AGAIN";
    secondary.action = static_cast<fui::ActionId>(ActionPlayAgain);
  } else {
    primary.action = model.canFire ? static_cast<fui::ActionId>(ActionFire) : fui::NO_ACTION;
    if (!model.canFire) primary.styles = toybox::disabledButtonStyles();
    secondary.label = model.surrenderArmed ? "CONFIRM" : "SURRENDER";
    secondary.action = model.canSurrender ? static_cast<fui::ActionId>(ActionSurrender) : fui::NO_ACTION;
    if (!model.canSurrender) secondary.styles = toybox::disabledButtonStyles();
  }

  screen.button(primary, left);
  screen.button(secondary, right);

  return screen.body();
}

}  // namespace bshipui
