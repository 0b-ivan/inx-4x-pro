#pragma once

// Keep the simulator's real esp_system compatibility header, but also expose
// esp_random() from our host stub. Firmware builds do not add sim-stubs to the
// include path, so this wrapper is simulator-only.
#include_next <esp_system.h>
#include "esp_random.h"
