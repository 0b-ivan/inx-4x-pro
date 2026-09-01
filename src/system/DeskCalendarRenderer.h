#pragma once

#include "system/SleepClockRenderer.h"

class GfxRenderer;

namespace DeskCalendarRenderer {

// Renders cached calendar data. Returns false when no usable cache is present,
// allowing the caller to fall back to the normal clock face.
bool render(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dateTime, int x, int y, int w, int h);

}  // namespace DeskCalendarRenderer
