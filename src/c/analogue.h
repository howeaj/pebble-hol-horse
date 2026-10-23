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

void analogue_angles(struct tm *tick_time, TimeUnits units_changed, HandAngles *angles);
void analogue_update_angles(struct tm *tick_time, TimeUnits units_changed);
HandAngles analogue_get_angles(void);
bool analogue_is_vertical(int32_t angle);
