#pragma once

#include "system/SleepClockRenderer.h"

class GfxRenderer;

namespace DeskCalendarRenderer {

// Renders the desk-calendar face. When no usable cache is present, the renderer
// shows a visible setup/status screen instead of silently falling back to a clock.
// syncAllowed is false for settings previews so opening the picker never starts Wi-Fi.
bool render(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dateTime, int x, int y, int w, int h,
            bool syncAllowed = true);

}  // namespace DeskCalendarRenderer
