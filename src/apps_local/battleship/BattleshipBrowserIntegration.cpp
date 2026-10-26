#include "BattleshipActivity.h"

#include <Logging.h>
#include <Memory.h>

#include "web/BattleshipLocalPlayActivity.h"

void BattleshipActivity::enterLink(const linkplay::GameId gameId) {
  auto chooser = makeUniqueNoThrow<BattleshipLocalPlayActivity>(renderer, mappedInput);
  if (!chooser) {
    // Allocation failure must not remove the already-shipped local multiplayer
    // path. Fall back to the exact LinkActivity entry that PLAY NEARBY used
    // before browser play existed.
    LOG_ERR("BSHIPWEB", "Could not allocate local-play chooser; falling back to X4 Pro link");
    linkplay::LinkActivity::enterLink(gameId);
    return;
  }

  startActivityForResult(std::move(chooser), [this, gameId](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* menu = std::get_if<MenuResult>(&result.data);
    if (menu == nullptr || menu->action != 0) return;

    // X4 PRO means exactly the old path. No browser state, Wi-Fi mode or game
    // state is shared with ESP-NOW; the chooser has already torn itself down.
    linkplay::LinkActivity::enterLink(gameId);
  });
}
