#include "TarotCardRenderer.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>

#include "TarotAssets.h"

namespace tarot {

bool drawCard(const GfxRenderer& renderer, const int card, const int x, const int y, const int width,
              const int height) {
  HalFile file;
  if (!Storage.openFileForRead("TAROT", TarotAssets::cardPath(card).c_str(), file)) {
    LOG_ERR("TAROT", "Failed to open card %d", card);
    return false;
  }
  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("TAROT", "Invalid card %d", card);
    return false;
  }

  const int sourceWidth = bitmap.getWidth();
  const int sourceHeight = bitmap.getHeight();
  if (sourceWidth <= 0 || sourceHeight <= 0) return false;
  const float scale = std::min(static_cast<float>(width) / static_cast<float>(sourceWidth),
                               static_cast<float>(height) / static_cast<float>(sourceHeight));

  auto outputRow = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>((sourceWidth + 3) / 4));
  auto rowBytes = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    LOG_ERR("TAROT", "OOM: card row buffers");
    return false;
  }

  const auto edge = [scale](const int index) { return static_cast<int>(std::floor(index * scale)); };

  for (int bmpY = 0; bmpY < sourceHeight; ++bmpY) {
    if (bitmap.readNextRow(outputRow.get(), rowBytes.get()) != BmpReaderError::Ok) {
      LOG_ERR("TAROT", "Failed to read row %d of card %d", bmpY, card);
      return false;
    }
    const int row = bitmap.isTopDown() ? bmpY : sourceHeight - 1 - bmpY;
    const int top = y + edge(row);
    const int rowHeight = std::max(1, edge(row + 1) - edge(row));

    for (int bmpX = 0; bmpX < sourceWidth; ++bmpX) {
      // readNextRow packs 2 bits per pixel; 3 is white.
      const uint8_t value = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;
      if (value == 3) continue;
      const int column = sourceWidth - 1 - bmpX;
      renderer.fillRect(x + edge(column), top, std::max(1, edge(column + 1) - edge(column)), rowHeight, true);
    }
  }

  renderer.preserveImagePolarity(x, y, edge(sourceWidth), edge(sourceHeight));
  return true;
}

void drawCardBack(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  // Clean slate so the lattice reads on e-ink.
  renderer.fillRect(x, y, width, height, false);

  // Double border, a hand-span in from the card edge.
  const int inset = std::max(6, width / 20);
  renderer.drawRect(x + inset, y + inset, width - 2 * inset, height - 2 * inset, 2, true);
  const int inner = inset + 8;
  renderer.drawRect(x + inner, y + inner, width - 2 * inner, height - 2 * inner, 1, true);

  // Diagonal diamond lattice inside the inner border. Each 45-degree line is
  // clipped to the box in the parameter t so no pixel lands outside it.
  const int left = x + inner + 4, top = y + inner + 4;
  const int right = x + width - inner - 4, bottom = y + height - inner - 4;
  const int span = bottom - top, run = right - left;
  const int step = std::max(16, width / 14);
  for (int d = -span; d < run; d += step) {
    const int tlo = std::max(0, -d), thi = std::min(span, run - d);
    if (tlo > thi) continue;
    // '\' family through (left+d, top), slope +1.
    renderer.drawLine(left + d + tlo, top + tlo, left + d + thi, top + thi, 1, true);
    // '/' family through (right-d, top), slope -1.
    renderer.drawLine(right - d - tlo, top + tlo, right - d - thi, top + thi, 1, true);
  }

  // Centre emblem: three concentric diamonds, the innermost filled.
  const int cx = x + width / 2, cy = y + height / 2;
  for (int r = step * 2, k = 0; r >= step / 2; r -= step, ++k) {
    renderer.drawLine(cx, cy - r, cx + r, cy, 1, true);
    renderer.drawLine(cx + r, cy, cx, cy + r, 1, true);
    renderer.drawLine(cx, cy + r, cx - r, cy, 1, true);
    renderer.drawLine(cx - r, cy, cx, cy - r, 1, true);
  }
  const int f = step / 2;
  for (int dy = -f; dy <= f; ++dy) {
    const int w = f - (dy < 0 ? -dy : dy);
    renderer.drawLine(cx - w, cy + dy, cx + w, cy + dy, 1, true);
  }
}

}  // namespace tarot
