#pragma once

class GfxRenderer;

namespace tarot {

// Draws card `card` from the SD deck scaled to fill the given box. The deck's
// BMPs are stored mirrored left to right, so the columns are drawn back in
// reverse; the rows keep the BMP's own orientation.
bool drawCard(const GfxRenderer& renderer, int card, int x, int y, int width, int height);

}  // namespace tarot
