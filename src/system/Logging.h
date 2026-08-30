#pragma once

/**
 * @file Logging.h
 * @brief Minimal FreeInk/CrossPoint logging compatibility for the Inx X4 Pro port.
 *
 * FreeInk hardware modules such as FrontlightManager use CrossPoint's LOG_*
 * macros. Inx intentionally keeps its existing Serial logging instead of
 * importing CrossPoint's crash-ring/Serial wrapper, so this header supplies the
 * small source-level contract those SDK modules need.
 */

#include <Arduino.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

#ifdef ENABLE_SERIAL_LOG

#define INX_LOG_PRINT(level, origin, format, ...) \
  Serial.printf("[%lu] [%s] [%s] " format "\n", millis(), level, origin, ##__VA_ARGS__)

#if LOG_LEVEL >= 0
#define LOG_ERR(origin, format, ...) INX_LOG_PRINT("ERR", origin, format, ##__VA_ARGS__)
#else
#define LOG_ERR(origin, format, ...)
#endif

#if LOG_LEVEL >= 1
#define LOG_INF(origin, format, ...) INX_LOG_PRINT("INF", origin, format, ##__VA_ARGS__)
#else
#define LOG_INF(origin, format, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_DBG(origin, format, ...) INX_LOG_PRINT("DBG", origin, format, ##__VA_ARGS__)
#else
#define LOG_DBG(origin, format, ...)
#endif

#else

#define LOG_ERR(origin, format, ...)
#define LOG_INF(origin, format, ...)
#define LOG_DBG(origin, format, ...)

#endif
