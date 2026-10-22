#include "QrGeneratorActivity.h"
#include "../Shelf.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../components/UITheme.h"
#include "fontIds.h"
#include "../../util/QrUtils.h"

void QrGeneratorActivity::onEnter() {
  Activity::onEnter();
  state = TEXT_INPUT;
  textPayload.clear();

  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "QR Code Text", "", 0),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          shelf::leave(renderer, mappedInput);
        } else {
          textPayload = std::get<KeyboardResult>(result.data).text;
          if (textPayload.empty()) {
            shelf::leave(renderer, mappedInput);
          } else {
            state = QR_DISPLAY;
            requestUpdate();
          }
        }
      });
}

void QrGeneratorActivity::onExit() { Activity::onExit(); }

void QrGeneratorActivity::loop() {
  if (state == QR_DISPLAY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      shelf::leave(renderer, mappedInput);
      return;
    }
    int tx_, ty_;
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasScreenTapped(tx_, ty_)) {
      state = TEXT_INPUT;
      startActivityForResult(
          std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "QR Code Text", textPayload, 0),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              state = QR_DISPLAY;
              requestUpdate();
            } else {
              textPayload = std::get<KeyboardResult>(result.data).text;
              if (textPayload.empty()) {
                shelf::leave(renderer, mappedInput);
              } else {
                state = QR_DISPLAY;
                requestUpdate();
              }
            }
          });
    }
  }
}

void QrGeneratorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == QR_DISPLAY) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QR_GENERATOR));

    const int availableWidth = pageWidth - 40;
    const int availableHeight =
        pageHeight - metrics.topPadding - metrics.headerHeight - metrics.verticalSpacing * 2 - metrics.buttonHintsHeight;
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

    const Rect qrBounds(20, startY, availableWidth, availableHeight);
    QrUtils::drawQrCode(renderer, qrBounds, textPayload);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "New", "", "");
    GUI.drawButtonHintsAlways(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
