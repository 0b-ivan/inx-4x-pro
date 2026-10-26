#pragma once
#include <string>
// Minimal BLE type stubs so hardware-only activity headers compile in the
// simulator. These classes are never instantiated — their .cpp files are
// excluded via build_src_filter.
struct BLEAdvertising {};
struct BLEClient {};
struct BLEAddress {
  explicit BLEAddress(const std::string& /*addr*/) {}
  explicit BLEAddress(const char* /*addr*/) {}
};
struct BLEScan {};
struct BLEDevice {
  static void init(const std::string& /*name*/) {}
  static BLEAdvertising* getAdvertising() { return nullptr; }
  static BLEScan* getScan() { return nullptr; }
  static BLEClient* createClient() { return nullptr; }
};
