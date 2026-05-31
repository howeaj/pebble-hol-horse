// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/// Analogue watch boilerplate

#include "analogue.h"

#include "macros.h"

#include <pebble.h>


static HandAngles s_hand_angles;


// Update hand angles from a tick handler
void analogue_update_angles(struct tm *tick_time, TimeUnits units_changed) {
    // +1 round up to ensure the result is always > the precise angle
    if (units_changed & MINUTE_UNIT) {
        const int32_t hour_as_minutes = (((tick_time->tm_hour % 12) * MINUTES_PER_HOUR) + tick_time->tm_min);
        s_hand_angles.hour = MUL_FRACT(hour_as_minutes, TRIG_MAX_ANGLE, 12 * MINUTES_PER_HOUR) + 1;
        s_hand_angles.min = MUL_FRACT(tick_time->tm_min, TRIG_MAX_ANGLE, MINUTES_PER_HOUR) + 1;
    }
    s_hand_angles.sec = MUL_FRACT(tick_time->tm_sec, TRIG_MAX_ANGLE, SECONDS_PER_MINUTE) + 1;
}

// Return the latest hand angles
HandAngles analogue_get_angles(void) {
    return s_hand_angles;
}

// Return true if angle is closer to vertical than horizontal
bool analogue_is_vertical(int32_t angle) {
    return (
        (angle < DEG_TO_TRIGANGLE(45))
        || WITHIN(angle, DEG_TO_TRIGANGLE(135), DEG_TO_TRIGANGLE(225))
        || (angle > DEG_TO_TRIGANGLE(315))
    );
}
