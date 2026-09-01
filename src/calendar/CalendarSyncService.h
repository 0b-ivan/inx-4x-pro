#pragma once

#include "system/SleepClockRenderer.h"

namespace CalendarSyncService {

// Runs a CalDAV refresh only when configured and due. Existing cache remains
// usable on every failure path.
bool syncIfDue(const SleepClockRenderer::DateTimeView& now);

}  // namespace CalendarSyncService
