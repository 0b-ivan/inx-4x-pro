#pragma once
// Persistence is replaced by an in-memory store in these orchestration tests.
struct JsonValue {
  template <class T>
  JsonValue& operator=(const T&) {
    return *this;
  }
  template <class T>
  T operator|(T fallback) const {
    return fallback;
  }
};
struct JsonDocument {
  JsonValue operator[](const char*) { return {}; }
};
struct JsonVariantConst {
  JsonValue operator[](const char*) const { return {}; }
};
