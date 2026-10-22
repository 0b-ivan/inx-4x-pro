#pragma once

// Single owner for radio transitions used by the local wireless applications.
// Developer Mode yields the radio before the activity acquires it.
class RadioManager {
 public:
  void ensureWifi();
  void ensureBle();
  void shutdown();

 private:
  enum class Mode { None, Wifi, Ble };
  Mode mode_ = Mode::None;
};

extern RadioManager RADIO;
