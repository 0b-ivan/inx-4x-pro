/**
 * @file HalDisplay.cpp
 * @brief Definitions for HalDisplay on the Xteink X4 Pro.
 */

#include <HalDisplay.h>
#include <HalGPIO.h>
#ifndef SIMULATOR
#include <Preferences.h>
#endif

HalDisplay::HalDisplay() : einkDisplay(-1, -1, -1, -1, -1, -1) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin() {
  einkDisplay.begin();
#ifndef SIMULATOR
  Preferences prefs;
  if (prefs.begin("inx-quick", true)) {
    einkDisplay.setInverted(prefs.getBool("night", false));
    prefs.end();
  }
#endif
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
    case HalDisplay::STRONG_FAST_REFRESH:
    case HalDisplay::MANUAL_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode) { einkDisplay.displayBuffer(convertRefreshMode(mode)); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::setInverted(const bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  (void)trackForRevert;
  einkDisplay.displayGrayBuffer(false, nullptr, quality);
}

void HalDisplay::displayGrayBufferFastQuality() { einkDisplay.displayGrayBuffer(false, nullptr, true); }

void HalDisplay::prepareQualityGrayscale() {
  // The legacy Inx hook was X3/X4-SDK specific. Current FreeInk drivers own
  // their grayscale preparation; no extra X4 Pro command sequence is injected.
}

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }

bool HalDisplay::deviceIsX3() const { return einkDisplay.isX3Mode(); }
