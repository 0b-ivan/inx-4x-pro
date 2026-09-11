from pathlib import Path
import re

# Battleship model: expose shared identity/progression on the front door.
path = Path("src/apps_local/battleship/BattleshipScreens.h")
text = path.read_text()
old = '''struct StartModel {
  // No game to continue means no CONTINUE row, rather than one that does
  // nothing.
  bool hasSavedGame = false;
  int played = 0;
  int won = 0;
  int streak = 0;
  int selected = 0;
};'''
new = '''struct StartModel {
  // Shared player identity/progression shown directly on the Battleship front
  // door. These values come from PlayerRuntime; Battleship no longer presents
  // its legacy local counters as if they were the global profile.
  const char* playerName = "GUEST";
  const char* playerRank = "";
  uint16_t playerLevel = 1;

  // No game to continue means no CONTINUE row, rather than one that does
  // nothing.
  bool hasSavedGame = false;
  int played = 0;
  int won = 0;
  int losses = 0;
  int draws = 0;
  int streak = 0;
  int selected = 0;
};'''
assert old in text, "StartModel source changed"
path.write_text(text.replace(old, new, 1))

# Battleship start screen: identity/rank on one line, shared W/L/D on the next.
path = Path("src/apps_local/battleship/BattleshipScreens.cpp")
text = path.read_text()
old = '''  // Your record, in one line, above a rule. Small: it is worth having but it is
  // not why you opened the app.
  char record[64];
  std::snprintf(record, sizeof(record), "%d PLAYED   %d WON   STREAK %d", model.played, model.won, model.streak);
  const fui::Rect line = screen.takeTop(26);
  fui::TextStyle recordStyle;
  recordStyle.font = toybox::kTileFont;
  recordStyle.align = fui::TextAlign::Left;
  screen.target().text(line, record, recordStyle);
  screen.target().fill(fui::makeRect(line.x, static_cast<int16_t>(line.bottom() + 6), line.width, toybox::kRule),
                       fui::Paint::solid(fui::Color::Black));'''
new = '''  // Identity is visible where the game starts. The first alpha stored global
  // progression correctly but still showed the old Battleship-local counters,
  // which made the player system effectively invisible.
  char identity[96]{};
  const char* rank = model.playerRank != nullptr && model.playerRank[0] != '\\0' ? model.playerRank : "NO RANK";
  std::snprintf(identity, sizeof(identity), "%s   LV %u   %s", model.playerName == nullptr ? "GUEST" : model.playerName,
                static_cast<unsigned>(model.playerLevel), rank);

  fui::TextStyle recordStyle;
  recordStyle.font = toybox::kTileFont;
  recordStyle.align = fui::TextAlign::Left;
  const fui::Rect identityLine = screen.takeTop(26);
  screen.target().text(identityLine, identity, recordStyle);

  char record[80]{};
  std::snprintf(record, sizeof(record), "%d W   %d L   %d D   STREAK %d", model.won, model.losses, model.draws,
                model.streak);
  const fui::Rect recordLine = screen.takeTop(26);
  screen.target().text(recordLine, record, recordStyle);
  screen.target().fill(
      fui::makeRect(recordLine.x, static_cast<int16_t>(recordLine.bottom() + 6), recordLine.width, toybox::kRule),
      fui::Paint::solid(fui::Color::Black));'''
assert old in text, "start menu record block source changed"
path.write_text(text.replace(old, new, 1))

# Battleship activity: initialize PlayerRuntime on entry and source the menu
# from shared stats instead of the legacy bship.cfg counters.
path = Path("src/apps_local/battleship/BattleshipActivity.cpp")
text = path.read_text()
assert '#include <algorithm>\n' in text
text = text.replace('#include <algorithm>\n', '#include <algorithm>\n#include <array>\n', 1)
old_inc = '#include "../Shelf.h"\n#include "../player/PlayerRuntime.h"\n'
new_inc = '#include "../Shelf.h"\n#include "../leaderboard/RankSystem.h"\n#include "../player/PlayerProgression.h"\n#include "../player/PlayerRuntime.h"\n'
assert old_inc in text, "Battleship includes changed"
text = text.replace(old_inc, new_inc, 1)
old_enter = '''  toybox::ensureFonts(renderer);
  // Mixed from the clock, so two games in a row are not the same game.'''
new_enter = '''  toybox::ensureFonts(renderer);
  // Make the device-wide identity available before the Battleship front door
  // is drawn. recordCurrentMatch() also initializes lazily, but that is too late
  // for a player name/rank that must be visible before the first shot.
  (void)player::runtime().begin();
  // Mixed from the clock, so two games in a row are not the same game.'''
assert old_enter in text, "onEnter source changed"
text = text.replace(old_enter, new_enter, 1)
old_model = '''bshipui::StartModel BattleshipActivity::startModel() const {
  bshipui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.played = played;
  model.won = won;
  model.streak = streak;
  model.selected = startIndex;
  return model;
}'''
new_model = '''bshipui::StartModel BattleshipActivity::startModel() const {
  bshipui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.selected = startIndex;

  // Keep the legacy counters only as a fallback if player storage is genuinely
  // unavailable. In normal operation the shared PlayerRuntime is the one source
  // of truth for what this screen shows.
  model.played = played;
  model.won = won;
  model.streak = streak;

  if (!player::runtime().ready()) return model;

  const player::Player* active = player::runtime().activePlayer();
  model.playerName = active == nullptr ? "GUEST" : active->name;

  std::array<player::GameStats, player::PlayerRuntime::kGameCount> stats{};
  size_t count = 0;
  if (player::runtime().currentStats(stats.data(), stats.size(), count) != player::StoreResult::Ok) return model;

  const player::ProgressionSnapshot progression = player::ProgressionSystem::summarize(stats.data(), count);
  model.playerLevel = progression.level;

  for (size_t i = 0; i < count; ++i) {
    if (stats[i].game != player::GameId::Battleship) continue;
    model.won = static_cast<int>(stats[i].wins);
    model.losses = static_cast<int>(stats[i].losses);
    model.draws = static_cast<int>(stats[i].draws);
    model.played = model.won + model.losses + model.draws;
    model.streak = static_cast<int>(stats[i].currentStreak);
    model.playerRank = leaderboard::RankSystem::forWins(stats[i].wins).name;
    break;
  }

  return model;
}'''
assert old_model in text, "startModel source changed"
path.write_text(text.replace(old_model, new_model, 1))

# The global player/profile UI existed but had no Shelf row.
path = Path("src/apps_local/Shelf.cpp")
text = path.read_text()
old = '''constexpr shelf::Item kApps[] = {
    {"STUDY", &icon_study_32, &StudyActivity::create},'''
new = '''constexpr shelf::Item kApps[] = {
    {"PLAYER", &icon_study_32, &PlayerActivity::create},
    {"STUDY", &icon_study_32, &StudyActivity::create},'''
assert old in text, "kApps source changed"
path.write_text(text.replace(old, new, 1))
