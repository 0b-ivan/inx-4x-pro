#pragma once

class GfxRenderer;

namespace tarot {

// Draws card `card` from the SD deck scaled to fill the given box. The deck's
// BMPs are stored mirrored left to right, so the columns are drawn back in
// reverse; the rows keep the BMP's own orientation.
bool drawCard(const GfxRenderer& renderer, int card, int x, int y, int width, int height);

// Draws the face-down card back: a bordered diamond lattice with a centre
// emblem. Procedural and 1-bit, so it needs no asset and shows even before the
// deck is downloaded -- it is the deck the player draws from.
void drawCardBack(const GfxRenderer& renderer, int x, int y, int width, int height);

}  // namespace tarot
