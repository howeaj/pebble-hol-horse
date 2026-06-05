// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// App-specific definitions of config options, for inclusion by config.h

#pragma once

#include <pebble.h>

#include "macros.h"


/** The persist data version for dealing with backwards-incompatible changes.
    This doesn't need to change as long as you only append new fields
    and don't change the messageKeys or meaning of existing data.
    Note lowest allowed value is 1.
*/
#define PERSIST_CONFIG_VERSION (1)

/** Macro table defining all config data.

    - Conftype must match one of the RECEIVE_CONFIG_ options in config.c
    - CType is the C-side storage type.
    - Message key must match config.json and package.json, and be a valid C identifier.
    - Default value must match config.json.
*/
#define X_CONFIG_OPTIONS(MACRO) \
    /* conftype, C type,              message key,                  default */ \
    MACRO(COLOR, GColor,              fgColor,                      GColorBlack) \
    MACRO(COLOR, GColor,              bgColor,                      GColorWhite) \
    MACRO(COLOR, GColor,              comp1FgColor,                 GColorPurple) \
    MACRO(COLOR, GColor,              comp1BgColor,                 GColorGreen) \
    MACRO(COLOR, GColor,              comp2FgColor,                 GColorPurple) \
    MACRO(COLOR, GColor,              comp2BgColor,                 GColorGreen) \
    MACRO(COLOR, GColor,              comp3FgColor,                 GColorPurple) \
    MACRO(COLOR, GColor,              comp3BgColor,                 GColorGreen) \
    MACRO(ENUM,  ComplicationType,    comp1Type,                    ComplicationType_Tick) \
    MACRO(ENUM,  ComplicationType,    comp2Type,                    ComplicationType_Tick) \
    MACRO(ENUM,  ComplicationType,    comp3Type,                    ComplicationType_Tick) \
/* end of X_CONFIG_OPTIONS */


typedef enum ComplicationType {
    ComplicationType_Tick = 0,
    ComplicationType_Battery = 1,
    ComplicationType_DayName = 2,
    ComplicationType_DayNum = 3,
    ComplicationType_MonthName = 4,
    ComplicationType_MonthNum = 5,
    ComplicationType_Steps = 6,
    ComplicationType_Sleep = 7,
    // TODO ComplicationType_Timezone
    // TODO temperature
    // TODO weather?
    // TODO bluetooth?
    // Heartrate not supported because PR2 doesn't have one
} ComplicationType;
