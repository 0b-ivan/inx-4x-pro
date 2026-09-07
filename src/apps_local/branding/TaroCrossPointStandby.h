#pragma once

#include <GfxRenderer.h>

namespace crossplay::branding {

// A small 1-bit-friendly standby mark for the X4 Pro. The broken ring nods to
// classic 8-bit industrial design, while the T bridge and cross-point node make
// it a distinct Taro/CrossPoint mark rather than a copy of the Commodore logo.
// Drawn from primitives: no heap allocation and no bitmap asset in RAM.
inline void drawTaroCrossPointMark(const GfxRenderer& renderer, const int cx, const int cy) {
  constexpr int RADIUS = 54;
  constexpr int RING_WIDTH = 12;

  // Full ring, then open the right side asymmetrically.
  renderer.drawArc(RADIUS, cx, cy, -1, -1, RING_WIDTH, true);
  renderer.drawArc(RADIUS, cx, cy, 1, -1, RING_WIDTH, true);
  renderer.drawArc(RADIUS, cx, cy, -1, 1, RING_WIDTH, true);
  renderer.drawArc(RADIUS, cx, cy, 1, 1, RING_WIDTH, true);
  renderer.fillRect(cx + 28, cy - 22, 34, 36, false);

  // Taro: a T bridges into the opening.
  renderer.fillRect(cx + 10, cy - 29, 49, 10, true);
  renderer.fillRect(cx + 29, cy - 20, 10, 49, true);

  // CrossPoint: offset cross plus a solid point/node.
  renderer.fillRect(cx + 43, cy + 6, 31, 8, true);
  renderer.fillRect(cx + 54, cy - 5, 8, 31, true);
  renderer.fillRect(cx + 52, cy + 4, 12, 12, true);
}

}  // namespace crossplay::branding
