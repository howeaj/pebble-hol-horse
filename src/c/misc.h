// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include "debug.h"
#include "macros.h"

#include <ctype.h>
#include <pebble.h>


void mark_root_window_dirty(bool full_redraw);


#if DEBUG
// Return milliseconds since the epoch
static inline uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}
#endif // DEBUG

static inline GPoint gpoint_sub(const GPoint a, const GPoint b) {
    return GPoint(a.x - b.x, a.y - b.y);
}

// set the value at `index` in `bitmap`'s palette to `color`
static inline void gbitmap_set_color(GBitmap* bitmap, size_t index, GColor color) {
    gbitmap_get_palette(bitmap)[index] = color;
}

// Return a GPoint that is `distance` away from `origin` at `angle`.
// If `origin` is 0, this is equivalent to converting `angle` to a cartesian vector of magnitude `distance`.
static inline GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.y
    };
}

// Return the angle from a to b in TRIGANGLE units
static inline int32_t angle_between_points(GPoint a, GPoint b) {
    return atan2_lookup(b.x - a.x, b.y - a.y);  // TODO why are x and y reversed ???????
}

// in-place string to upper case
static inline void string_to_upper(char* str, size_t n) {
    for (size_t i = 0; (i < n) && str[i]; i++){
        str[i] = toupper(str[i]);
    }
}