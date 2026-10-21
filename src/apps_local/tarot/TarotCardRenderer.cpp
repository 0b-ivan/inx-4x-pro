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

}  // namespace tarot
