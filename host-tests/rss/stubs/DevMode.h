#pragma once
namespace devmode {
inline bool enabled = false, held = false;
inline bool inhibitsSleep() { return enabled; }
inline bool holdsRadio() { return held; }
}  // namespace devmode
