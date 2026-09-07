#pragma once

namespace passkey {

// Device-only TinyUSB FIDO HID bridge. Normal CrossPlay builds keep their
// current USB-Serial/JTAG setup; this bridge exists only in x4pro_passkey.
class UsbPasskeyTransport {
 public:
  bool begin();
  void poll();
  bool ready() const;
  bool started() const { return started_; }
  unsigned long packetsSeen() const { return packetsSeen_; }

 private:
  bool started_ = false;
  unsigned long packetsSeen_ = 0;
};

UsbPasskeyTransport& usbPasskey();

}  // namespace passkey
