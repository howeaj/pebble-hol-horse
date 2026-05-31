// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/// Analogue watch boilerplate

#pragma once

#include <pebble.h>


// Latest angles for clock hands, in TRIGANGLE units
typedef struct HandAngles {
    int32_t hour;
    int32_t min;
    int32_t sec;
} HandAngles;

// Update hand angles from a tick handler
void analogue_update_angles(struct tm *tick_time, TimeUnits units_changed);

// Return the latest hand angles
HandAngles analogue_get_angles(void);

// Return true if angle is closer to vertical than horizontal
bool analogue_is_vertical(int32_t angle);
