// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Generic C macros.

#pragma once

#include "debug.h"

// Macros
#define MACRO_START do {
#define MACRO_END } while (0)

// Misc
#define UNUSED(x) ((void)(x))
#define ARRAY_LEN(array) (sizeof((array)) / sizeof((array)[0]))  // number of elements in the array

// Maths
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ABS(a) (((a) >= 0) ? (a) : ((a) * -1))
#define ABSDIFF(a, b) ((a) >= (b) ? (a) - (b) : (b) - (a))
#define ABSDIFF_WRAP(a, b, max) MIN(ABS((a) - (b)), (max) + 1 - ABS((a) - (b)))
#define SIGN(a) (((a) >= 0) ? 1 : -1)
#define SQUARE(a) ((a) * (a))
#define MUL_FRACT(val, numerator, denominator) (((val) * (numerator)) / (denominator))
#define WITHIN(a, min, max) (((a) >= (min)) && ((a) <= (max)))

// Debugging
#if DEBUG
    #define LOG(...) APP_LOG(APP_LOG_LEVEL_INFO, __VA_ARGS__)
    #define LOG_ERR(...) APP_LOG(APP_LOG_LEVEL_ERROR, __VA_ARGS__)
    #define TRACE(...) APP_LOG(APP_LOG_LEVEL_DEBUG, __VA_ARGS__)
    #define ASSERT(condition) if (!(condition)) APP_LOG(APP_LOG_LEVEL_ERROR, \
        "ASSERTION FAILED AT %s:%d - "#condition, __FILE__, __LINE__)
#else // !DEBUG
    #define LOG(...)
    #define LOG_ERR(...)
    #define TRACE(...)
    #define ASSERT(condition) ((void)(condition))
#endif // !DEBUG

// Profiling
#if PROFILE
    #define PROFILE_START() const uint32_t _PROFILE_start_ms = timestamp_ms()
    #define PROFILE_END(name) MACRO_START \
        const uint32_t _PROFILE_end_ms = timestamp_ms(); \
        LOG(name": %ums", _PROFILE_end_ms - _PROFILE_start_ms); \
        MACRO_END
#else // !PROFILE
    #define PROFILE_START()
    #define PROFILE_END(name)
#endif // !PROFILE

#define STATIC_ASSERT(condition) _Static_assert((condition), #condition)

// Strings
#define BOOL_TO_STR(boolean) (boolean ? "true" : "false")

// Constants
#define TIME_MAX INT32_MAX  // max value of time_t
#define MS_PER_S (1000)  // milliseconds per second

// Conversions
#define MIN_TO_TRIGANGLE(mins) DEG_TO_TRIGANGLE((360 / MINUTES_PER_HOUR) * mins)
#define SEC_TO_TRIGANGLE(secs) MIN_TO_TRIGANGLE(secs)
