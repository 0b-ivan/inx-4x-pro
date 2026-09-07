#include "PasskeyUsb.h"

#include "PasskeyCtap.h"

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>

#include <cstring>

namespace passkey {
namespace {

USBHID g_hid;
QueueHandle_t g_rxQueue = nullptr;
CtapHidAssembler g_assembler;
CtapProcessor g_processor;

static const uint8_t kFidoReportDescriptor[] = {
    0x06, 0xd0, 0xf1,        // Usage Page (FIDO Alliance)
    0x09, 0x01,              // Usage (U2F Authenticator Device)
    0xa1, 0x01,              // Collection (Application)
    0x09, 0x20,              // Usage (Input Report Data)
    0x15, 0x00,              // Logical Minimum (0)
    0x26, 0xff, 0x00,        // Logical Maximum (255)
    0x75, 0x08,              // Report Size (8)
    0x95, 0x40,              // Report Count (64)
    0x81, 0x02,              // Input (Data,Var,Abs)
    0x09, 0x21,              // Usage (Output Report Data)
    0x15, 0x00,              // Logical Minimum (0)
    0x26, 0xff, 0x00,        // Logical Maximum (255)
    0x75, 0x08,              // Report Size (8)
    0x95, 0x40,              // Report Count (64)
    0x91, 0x02,              // Output (Data,Var,Abs)
    0xc0                     // End Collection
};

class FidoHidDevice final : public USBHIDDevice {
 public:
  FidoHidDevice() {
    static bool added = false;
    if (!added) {
      added = true;
      g_hid.addDevice(this, sizeof(kFidoReportDescriptor));
    }
  }

  uint16_t _onGetDescriptor(uint8_t* buffer) override {
    std::memcpy(buffer, kFidoReportDescriptor, sizeof(kFidoReportDescriptor));
    return sizeof(kFidoReportDescriptor);
  }

  void _onOutput(uint8_t, const uint8_t* buffer, uint16_t len) override {
    if (g_rxQueue == nullptr || buffer == nullptr || len != kHidReportBytes) return;
    uint8_t frame[kHidReportBytes];
    std::memcpy(frame, buffer, sizeof(frame));
    xQueueSend(g_rxQueue, frame, 0);
  }
};

FidoHidDevice g_device;

bool sendMessage(const HidMessage& message) {
  constexpr std::size_t kMaxReports = 1U + ((kMaxMessageBytes > 57U ? kMaxMessageBytes - 57U : 0U) + 58U) / 59U;
  static std::array<uint8_t, kMaxReports * kHidReportBytes> reports{};
  const std::size_t count = encodeReports(message, reports.data(), reports.size());
  if (count == 0) return false;

  for (std::size_t i = 0; i < count; ++i) {
    unsigned wait = 0;
    while (!g_hid.ready() && wait++ < 100) delay(1);
    if (!g_hid.ready()) return false;
    if (!g_hid.SendReport(0, reports.data() + i * kHidReportBytes, kHidReportBytes)) return false;
  }
  return true;
}

}  // namespace

bool UsbPasskeyTransport::begin() {
  if (started_) return true;
  if (g_rxQueue == nullptr) g_rxQueue = xQueueCreate(8, kHidReportBytes);
  if (g_rxQueue == nullptr) return false;

  g_hid.begin();
  USB.manufacturerName("CrossPlay");
  USB.productName("CrossPlay Passkey");
  USB.serialNumber("X4PRO-PASSKEY");
  if (!USB.begin()) return false;

  started_ = true;
  return true;
}

void UsbPasskeyTransport::poll() {
  if (!started_ || g_rxQueue == nullptr) return;

  uint8_t frame[kHidReportBytes];
  while (xQueueReceive(g_rxQueue, frame, 0) == pdTRUE) {
    ++packetsSeen_;
    HidMessage request;
    uint8_t framingError = 0;
    if (!g_assembler.ingest(frame, sizeof(frame), request, framingError)) {
      if (framingError != 0) {
        HidMessage error;
        error.cid = (static_cast<uint32_t>(frame[0]) << 24U) | (static_cast<uint32_t>(frame[1]) << 16U) |
                    (static_cast<uint32_t>(frame[2]) << 8U) | frame[3];
        error.command = 0x3f;
        error.length = 1;
        error.payload[0] = framingError;
        sendMessage(error);
      }
      continue;
    }

    HidMessage response;
    if (g_processor.process(request, response)) sendMessage(response);
  }
}

bool UsbPasskeyTransport::ready() const { return started_ && g_hid.ready(); }

UsbPasskeyTransport& usbPasskey() {
  static UsbPasskeyTransport transport;
  return transport;
}

}  // namespace passkey

#else

namespace passkey {

bool UsbPasskeyTransport::begin() { return false; }
void UsbPasskeyTransport::poll() {}
bool UsbPasskeyTransport::ready() const { return false; }
UsbPasskeyTransport& usbPasskey() {
  static UsbPasskeyTransport transport;
  return transport;
}

}  // namespace passkey

#endif
