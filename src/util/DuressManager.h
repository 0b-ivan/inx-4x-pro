#pragma once

#include <atomic>
#include <cstring>
#include <string>

// Ported from biscuit_x3/src/util/DuressManager.h. CrossPlay's SD paths
// are under /crossplay rather than /biscuit.
class DuressManager {
 public:
  void activate() { duressActive.store(true, std::memory_order_release); }
  void deactivate() { duressActive.store(false, std::memory_order_release); }
  bool isActive() const { return duressActive.load(std::memory_order_acquire); }
  bool active() const { return isActive(); }

  std::string resolvePath(const char* path) const {
    if (path == nullptr) return {};
    if (isActive() && std::strncmp(path, "/crossplay/", 11) == 0) {
      return std::string("/crossplay_safe/") + (path + 11);
    }
    return path;
  }

 private:
  std::atomic<bool> duressActive{false};
};

extern DuressManager DURESS;
