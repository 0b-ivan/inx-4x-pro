#!/bin/bash
# Build release locally and publish to GitHub
set -euo pipefail

VERSION=$(awk '/^\[crossplay\]/{f=1;next} /^\[/{f=0} f && /^version *=/{print $3; exit}' platformio.ini)
TAG="v${VERSION}"

echo "🚀 Building Release ${TAG}"
echo "================================"

# Build x4pro
echo "📦 Building x4pro..."
pio run -e gh_release_x4pro
X4PRO_BIN=".pio/build/gh_release_x4pro/firmware.bin"
echo "✅ x4pro built: $X4PRO_BIN"

# Build sticky
echo "📦 Building sticky..."
pio run -e gh_release_sticky
STICKY_BIN=".pio/build/gh_release_sticky/firmware.bin"
echo "✅ sticky built: $STICKY_BIN"

# Get bootloader and partition table
BOOTLOADER=".pio/build/gh_release_x4pro/bootloader.bin"
PART_TABLE=".pio/build/gh_release_x4pro/partitions.bin"

# Merge x4pro
echo "🔗 Merging x4pro firmware..."
python3 -m esptool merge_bin \
  -o firmware.bin \
  --target esp32s3 \
  0x0 "$BOOTLOADER" \
  0x8000 "$PART_TABLE" \
  0x10000 "$X4PRO_BIN" \
  --flash_mode dio \
  --flash_size 16MB \
  --flash_freq 80m

# Merge sticky
echo "🔗 Merging sticky firmware..."
python3 -m esptool merge_bin \
  -o firmware-sticky.bin \
  --target esp32s3 \
  0x0 "$BOOTLOADER" \
  0x8000 "$PART_TABLE" \
  0x10000 "$STICKY_BIN" \
  --flash_mode dio \
  --flash_size 16MB \
  --flash_freq 80m

echo "✅ Merged binaries ready"
echo "  - firmware.bin ($(stat -f%z firmware.bin 2>/dev/null || stat -c%s firmware.bin 2>/dev/null) bytes)"
echo "  - firmware-sticky.bin ($(stat -f%z firmware-sticky.bin 2>/dev/null || stat -c%s firmware-sticky.bin 2>/dev/null) bytes)"

# Create release on GitHub
echo "📤 Publishing to GitHub..."
if gh release view "$TAG" &>/dev/null; then
  echo "Release $TAG exists, deleting..."
  gh release delete "$TAG" --yes
fi

NOTES=$(grep -A 100 "### What is new in ${VERSION}" .github/workflows/crossplay-release.yml | head -50 || echo "Release v${VERSION}")

gh release create "$TAG" \
  --title "CrossPlay v${VERSION}" \
  --notes "$NOTES" \
  firmware.bin \
  firmware-sticky.bin

echo "🎉 Release v${VERSION} published!"
echo "🔗 https://github.com/0b-ivan/inx-4x-pro/releases/tag/${TAG}"
