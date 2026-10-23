# Native Bitmap-Preview aus Biscuit X3

`BitmapRenderer.h` ist eine Kopie des Render-Mocks aus
[`0b-ivan/biscuit_x3`](https://github.com/0b-ivan/biscuit_x3/blob/master/test/mocks/BitmapRenderer.h)
(Source-Blob: `ed33ae9395d66322ebcab0400fd3d80433792f01`).

`bash host-tests/biscuit-preview/run.sh` compiliert den C++17-Mock ohne
Arduino oder Hardware. Es prüft die BMP-Abmessungen und 1-Bit-Farben,
reproduzierbare Zeichenausgaben, unterscheidbare Vorschauen und den
leeren Framebuffer. Die BMPs liegen in `.pio/biscuit-native-previews/`.

**Abgrenzung:** Die Vorschauen sind bewusst nur einfache Mock-Szenen und
testen **nicht** das echte `Activity::render()`. Diese Aufgabe übernimmt die
bereits vorhandene SDL-End-to-End-Suite; für mehrere ihrer Screenshots gibt
es zusätzlich eingefrorene Pixelreferenzen.
