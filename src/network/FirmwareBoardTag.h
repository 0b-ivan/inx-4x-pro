#pragma once

#include <cstddef>
#include <cstdint>

// Board-identity tag embedded in every CrossPoint image, plus a streaming
// scanner the firmware update paths use to reject an image built for a
// different board before it can boot and drive another device's pins. All the
// S3 boards (sticky, x4pro, papermono, ...) share a chip_id, so the existing
// esp_image_header chip check cannot tell them apart.
//
// The tag is "CROSSPOINT-BOARD-V1:<board>;" stored once in .rodata — the
// scanner's needle references the same array, so a CrossPoint image contains
// exactly one occurrence. Images without a tag (other projects, forks, older
// releases) are allowed: the guard only rejects a tag naming a DIFFERENT
// board.

#if FREEINK_DEVICE_X4PRO
#define CROSSPOINT_BOARD_NAME "x4pro"
#elif FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
#define CROSSPOINT_BOARD_NAME "x4"
#elif FREEINK_DEVICE_PAPERMONO
#define CROSSPOINT_BOARD_NAME "papermono"
#elif FREEINK_DEVICE_STICKY
#define CROSSPOINT_BOARD_NAME "sticky"
#elif FREEINK_DEVICE_M5PAPER
#define CROSSPOINT_BOARD_NAME "m5paper"
#elif FREEINK_DEVICE_LILYGO
#define CROSSPOINT_BOARD_NAME "lilygo"
#elif FREEINK_DEVICE_M5
#define CROSSPOINT_BOARD_NAME "m5"
#elif FREEINK_DEVICE_MURPHY
#define CROSSPOINT_BOARD_NAME "murphy"
#elif FREEINK_DEVICE_DELINK
#define CROSSPOINT_BOARD_NAME "delink"
#else
#error "FirmwareBoardTag: no FREEINK_DEVICE_* flag set; cannot derive board name"
#endif

// The passkey firmware is intentionally a separate OTA channel. Normal X4 Pro
// units keep requesting firmware.bin; passkey units request firmware-passkey.bin.
// A release may therefore carry both without allowing a normal device to switch
// USB personality accidentally.
#if defined(CROSSPOINT_USB_PASSKEY) && CROSSPOINT_USB_PASSKEY
#define CROSSPOINT_RELEASE_ASSET "firmware-passkey.bin"
#elif FREEINK_DEVICE_X4PRO
#define CROSSPOINT_RELEASE_ASSET "firmware.bin"
#else
#define CROSSPOINT_RELEASE_ASSET "firmware-" CROSSPOINT_BOARD_NAME ".bin"
#endif

namespace board_tag {

extern const char TAG[];
const char* boardName();
size_t boardNameLen();

class Scanner {
 public:
  void feed(const uint8_t* data, size_t len);
  bool mismatch() const { return mismatchFound; }
  const char* foundName() const { return mismatchFound ? captured : ""; }

 private:
  static constexpr size_t MAX_NAME = 23;
  char captured[MAX_NAME + 1] = {0};
  size_t nameLen = 0;
  size_t magicMatched = 0;
  bool capturing = false;
  bool mismatchFound = false;
};

}  // namespace board_tag
