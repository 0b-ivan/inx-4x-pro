// Native BitmapRenderer smoke tests, using the renderer mock from Biscuit X3.
// These validate the original preview tool, not the real activity UI.
#include "BitmapRenderer.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<uint8_t> bytes(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  assert(file && "BMP preview not written");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static size_t inkCount(const std::vector<uint8_t>& bitmap) {
  constexpr size_t bytesPerRow = 60;
  assert(bitmap.size() == 62 + bytesPerRow * 800);
  assert(bitmap[0] == 'B' && bitmap[1] == 'M');
  assert(bitmap[10] == 62 && bitmap[28] == 1); // pixel data and monochrome depth
  size_t ink = 0;
  for (size_t i = 62; i < bitmap.size(); ++i) {
    uint8_t p = bitmap[i];
    while (p) { ink += p & 1U; p >>= 1U; }
  }
  return ink;
}

static void probePreview(GfxRenderer& r) {
  r.clearScreen();
  r.drawCenteredText(12, 12, "Probe Sniffer", true, 1);
  r.drawLine(12, 42, 468, 42);
  r.drawText(10, 20, 90, "LabNetwork   -45 dBm");
  r.drawText(10, 20, 135, "Broadcast    -73 dBm");
  r.fillRect(10, 200, 460, 46, true);
  r.drawText(10, 22, 215, "GuestLab  02:00:00:00:00:33", false);
  r.drawLine(0, 768, 480, 768);
  r.drawCenteredText(8, 776, "Back       Select");
  r.displayBuffer();
}

static void crowdPreview(GfxRenderer& r) {
  r.clearScreen();
  r.drawCenteredText(12, 12, "Crowd Density", true, 1);
  r.drawLine(12, 42, 468, 42);
  r.drawCenteredText(12, 180, "3", true, 1);
  r.drawCenteredText(10, 220, "unique probe MACs");
  r.fillRect(65, 560, 110, 120, true);
  r.fillRect(200, 620, 110, 60, true);
  r.drawLine(40, 680, 450, 680);
  r.drawCenteredText(8, 776, "Back");
  r.displayBuffer();
}

int main(int argc, char** argv) {
  assert(argc == 2 && "usage: test_preview OUTPUT_DIR");
  const std::string output = argv[1];
  GfxRenderer renderer;
  probePreview(renderer);
  assert(renderer.saveBMP((output + "/probe.bmp").c_str()));
  const auto first = bytes(output + "/probe.bmp");
  probePreview(renderer);
  assert(renderer.saveBMP((output + "/probe_repeat.bmp").c_str()));
  assert(first == bytes(output + "/probe_repeat.bmp") &&
         "original X3 preview renderer must be deterministic");

  crowdPreview(renderer);
  assert(renderer.saveBMP((output + "/crowd.bmp").c_str()));
  const auto second = bytes(output + "/crowd.bmp");
  assert(first != second && "distinct preview scenes must render differently");
  assert(inkCount(first) > 400 && inkCount(second) > 400);

  renderer.clearScreen();
  assert(renderer.saveBMP((output + "/blank.bmp").c_str()));
  assert(inkCount(bytes(output + "/blank.bmp")) == 0);
  std::cout << "PASS Biscuit X3 native bitmap previews: dimensions, pixels, "
               "determinism, distinct views, blank screen\n";
  return 0;
}
