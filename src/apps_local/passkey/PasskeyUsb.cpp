#include "PasskeyUsb.h"

#include "PasskeyCtap.h"
#include "PasskeyStore.h"

#if defined(CROSSPOINT_USB_PASSKEY) && !defined(SIMULATOR)
#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>

#include <cstring>

namespace passkey {
namespace {

USBHID g_hid;
QueueHandle_t g_rxQueue = nullptr;
SemaphoreHandle_t g_pollMutex = nullptr;
CtapHidAssembler g_assembler;
CtapProcessor g_processor;
TaskHandle_t g_passkeyTask = nullptr;

static const uint8_t kFidoReportDescriptor[] = {
    0x06, 0xd0, 0xf1, 0x09, 0x01, 0xa1, 0x01,
    0x09, 0x20, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x21, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xc0
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

void sendPresenceKeepAlive(const uint32_t cid) {
  HidMessage message;
  message.cid = cid;
  message.command = 0x3b;  // CTAPHID_KEEPALIVE
  message.length = 1;
  message.payload[0] = 0x02;  // STATUS_UPNEEDED
  (void)sendMessage(message);
}

void passkeyTask(void*) {
  for (;;) {
    usbPasskey().poll();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace

bool UsbPasskeyTransport::begin() {
  if (started_) return true;
  if (g_rxQueue == nullptr) g_rxQueue = xQueueCreate(8, kHidReportBytes);
  if (g_pollMutex == nullptr) g_pollMutex = xSemaphoreCreateMutex();
  if (g_rxQueue == nullptr || g_pollMutex == nullptr) return false;

  (void)credentialStore().begin();
  g_processor.setKeepAliveSender(sendPresenceKeepAlive);

  g_hid.begin();
  USB.manufacturerName("CrossPlay");
  USB.productName("CrossPlay Passkey");
  USB.serialNumber("X4PRO-PASSKEY");
  if (!USB.begin()) return false;

  started_ = true;
  if (g_passkeyTask == nullptr) {
    if (xTaskCreate(passkeyTask, "passkey-usb", 4096, nullptr, 2, &g_passkeyTask) != pdPASS) {
      started_ = false;
      g_passkeyTask = nullptr;
      return false;
    }
  }
  return true;
}

void UsbPasskeyTransport::poll() {
  if (!started_ || g_rxQueue == nullptr || g_pollMutex == nullptr) return;
  if (xSemaphoreTake(g_pollMutex, 0) != pdTRUE) return;

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

  xSemaphoreGive(g_pollMutex);
}

bool UsbPasskeyTransport::ready() const { return started_ && g_hid.ready(); }

UsbPasskeyTransport& usbPasskey() {
  static UsbPasskeyTransport transport;
  return transport;
}

}  // namespace passkey

extern "C" void initVariant() { passkey::usbPasskey().begin(); }

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
