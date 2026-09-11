from pathlib import Path


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    file = Path(path)
    text = file.read_text()
    found = text.count(old)
    if found < count:
        raise SystemExit(f"{path}: expected at least {count} occurrence(s), found {found}")
    file.write_text(text.replace(old, new, count))


header = "src/apps_local/battleship/web/BattleshipLocalPlayActivity.h"
source = "src/apps_local/battleship/web/BattleshipLocalPlayActivity.cpp"

replace(
    header,
    '''  void fireX4Shot();
  void reportLastShot(bool browserShot);
''',
    '''  void fireX4Shot();
  void reportLastShot(bool browserShot);
  void recordMatchIfFinished();
''')

replace(
    header,
    '''  bool x4SurrenderArmed_ = false;
  bool devModePaused_ = false;
''',
    '''  bool x4SurrenderArmed_ = false;
  // Browser callbacks and device input can observe the same terminal state on
  // different loop passes. Only the first observation may book progression.
  bool resultRecorded_ = false;
  bool devModePaused_ = false;
''')

replace(
    source,
    '''#include "../../player/PlayerAvatar.h"
#include "../../ui/Toybox.h"
''',
    '''#include "../../player/PlayerAvatar.h"
#include "../../player/PlayerRuntime.h"
#include "../../ui/Toybox.h"
''')

replace(
    source,
    '''        activity.browser_.publish(bshipweb::browserSnapshot(activity.browserGame_, activity.browser_.clientSeen()));
        activity.requestUpdate();
''',
    '''        activity.recordMatchIfFinished();
        activity.browser_.publish(bshipweb::browserSnapshot(activity.browserGame_, activity.browser_.clientSeen()));
        activity.requestUpdate();
''')

replace(
    source,
    '''  x4AimCell_ = -1;
  x4SurrenderArmed_ = false;
  x4Report_[0] = '\\0';
  std::snprintf(x4Status_, sizeof(x4Status_), "WAITING FOR %s", browserPlayer_.name());
  stage_ = Stage::Playing;
''',
    '''  x4AimCell_ = -1;
  x4SurrenderArmed_ = false;
  resultRecorded_ = false;
  x4Report_[0] = '\\0';
  std::snprintf(x4Status_, sizeof(x4Status_), "WAITING FOR %s", browserPlayer_.name());
  stage_ = Stage::Playing;
''')

replace(
    source,
    '''  reportLastShot(false);
  seenLastShot_ = browserGame_.lastShot;
  x4AimCell_ = -1;
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
''',
    '''  reportLastShot(false);
  seenLastShot_ = browserGame_.lastShot;
  x4AimCell_ = -1;
  recordMatchIfFinished();
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
''')

replace(
    source,
    '''  x4SurrenderArmed_ = false;
  x4AimCell_ = -1;
  std::snprintf(x4Report_, sizeof(x4Report_), "YOU SURRENDERED");
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
''',
    '''  x4SurrenderArmed_ = false;
  x4AimCell_ = -1;
  std::snprintf(x4Report_, sizeof(x4Report_), "YOU SURRENDERED");
  recordMatchIfFinished();
  browser_.publish(bshipweb::browserSnapshot(browserGame_, browser_.clientSeen()));
''')

replace(
    source,
    '''void BattleshipLocalPlayActivity::routeChoiceInput() {
''',
    '''void BattleshipLocalPlayActivity::recordMatchIfFinished() {
  if (resultRecorded_ || stage_ != Stage::Playing || !bship::over(browserGame_)) return;
  resultRecorded_ = true;

  const bool x4Won = bship::winner(browserGame_) == 0;
  const player::PlayerServiceResult progression = player::runtime().recordCurrentMatch(
      player::GameId::Battleship, x4Won ? player::MatchOutcome::Win : player::MatchOutcome::Loss);
  if (progression != player::PlayerServiceResult::Ok) {
    LOG_ERR("BSHIPWEB", "player progression failed: %d", static_cast<int>(progression));
  }
}

void BattleshipLocalPlayActivity::routeChoiceInput() {
''')
