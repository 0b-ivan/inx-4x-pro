#pragma once
// ESP32's portmacro.h defines portMUX_TYPE and critical-section primitives.
// The simulator's FreeRTOS.h already provides all of these.
#include <freertos/FreeRTOS.h>
