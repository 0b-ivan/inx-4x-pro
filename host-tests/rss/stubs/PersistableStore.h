#pragma once
template <class T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T value;
    return value;
  }
  bool saveToFile() { return true; }
  void loadFromFile() {}
};
