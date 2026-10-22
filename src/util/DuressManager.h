#pragma once

#include <atomic>

// Session-local duress flag, raised by the alternate PIN.
// Consumers may query active() to switch into their duress behaviour.
// This flag does not itself wipe files or claim that a device alert was sent.
class DuressManager {
 public:
  void activate() { active_.store(true, std::memory_order_release); }
  void deactivate() { active_.store(false, std::memory_order_release); }
  bool active() const { return active_.load(std::memory_order_acquire); }

 private:
  std::atomic<bool> active_{false};
};

extern DuressManager DURESS;
