#pragma once
// Lightweight per-task breadcrumbs retained over a warm ESP32 reset.
// Each path owns its own phase field; callbacks must never scan the heap.
#include <cstdint>

namespace meshdiag {
enum Slot : uint8_t {
  LOOP_PHASE = 0, RENDER_PHASE, RECV_PHASE,
  PRESENCE_SENT, RENDER_DONE, RECV_DONE, COUNT
};
extern volatile uint32_t rtc[COUNT];
extern uint32_t previous[COUNT];
}  // namespace meshdiag
