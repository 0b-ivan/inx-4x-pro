#pragma once

#include <cstdint>

#include "system/SleepClockRenderer.h"

namespace CalendarSyncService {

// Runs a CalDAV refresh only when configured and due. Existing cache remains
// usable on every failure path.
bool syncIfDue(const SleepClockRenderer::DateTimeView& now);

// Returns the next useful timer wake in seconds for desk-calendar mode.
// Wakes no later than local midnight so the displayed date can roll over even
// when the configured network sync interval is longer.
uint32_t nextWakeSeconds(const SleepClockRenderer::DateTimeView& now);

}  // namespace CalendarSyncService
