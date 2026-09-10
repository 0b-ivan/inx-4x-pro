#include "BrowserPlayer.h"

#include <limits>

namespace bshipweb {
void BrowserPlayer::reset() {
  view_ = {};
  name_[0] = 0;
}

bool BrowserPlayer::apply(bship::Game& game, const Command& c) {
  constexpr int side = 1;
  if (c.revision != view_.revision || view_.revision == std::numeric_limits<uint32_t>::max()) return false;

  if (c.kind == CommandKind::Resume) {
    // Transport verifies the resume capability. Application state only accepts
    // reconnects once the browser fleet has been committed, so an unfinished
    // placement can never be resurrected after disconnect.
    return view_.profile && view_.ready && game.side[side].placed;
  }

  if (c.kind == CommandKind::Rematch) {
    if (!view_.profile || !view_.ready || !bship::over(game)) return false;
    bship::reset(game);
    game.turn = side;
    view_.ready = false;
    for (int i = 0; i < 5; ++i) {
      view_.bow[i] = 255;
      view_.horizontal[i] = 1;
    }
    ++view_.revision;
    return true;
  }

  if (bship::over(game)) return false;

  if (c.kind == CommandKind::Fire) {
    if (!view_.ready || !bship::bothPlaced(game) || game.turn != side || c.value[0] >= bship::kCells) return false;
    if (bship::shotAt(game.side[0], c.value[0])) return false;
    if (!bship::fire(game, c.value[0])) return false;
    ++view_.revision;
    return true;
  }

  if (view_.ready || game.side[side].placed || bship::bothPlaced(game)) return false;

  if (c.kind == CommandKind::Profile) {
    player::Name parts;
    for (int i = 0; i < 3; ++i) {
      if (c.value[i] > 13 || c.value[i] >= player::wordCount(i)) return false;
      parts.word[i] = c.value[i];
    }
    for (int i = 0; i < 3; ++i) view_.slots[i] = parts.word[i];
    player::compose(name_, sizeof(name_), parts);
    view_.profile = true;
  } else {
    if (!view_.profile || game.turn != side) return false;
    bship::Fleet draft;
    for (int i = 0; i < 5; ++i) draft.ships[i] = {view_.bow[i], view_.horizontal[i]};
    if (c.kind == CommandKind::Place) {
      if (c.value[0] >= 5 || c.value[1] >= 100 || c.value[2] > 1) return false;
      const bship::Ship ship{c.value[1], c.value[2]};
      if (!bship::canPlace(draft, c.value[0], ship)) return false;
      view_.bow[c.value[0]] = ship.bow;
      view_.horizontal[c.value[0]] = ship.horizontal;
    } else if (c.kind == CommandKind::Ready) {
      if (!bship::place(game, side, draft)) return false;
      view_.ready = true;
    } else {
      return false;
    }
  }

  ++view_.revision;
  return true;
}
}  // namespace bshipweb
