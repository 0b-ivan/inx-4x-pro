#pragma once
inline int testSavesUntilFailure = -1;
template <class T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T value;
    return value;
  }
  bool saveToFile() {
    if (testSavesUntilFailure == 0) return false;
    if (testSavesUntilFailure > 0) --testSavesUntilFailure;
    return true;
  }
  void loadFromFile() {}
};
