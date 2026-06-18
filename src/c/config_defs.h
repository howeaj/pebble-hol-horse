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

#define FACE_TEXT_SIZE (11)  // max size of the branding custom text (including NULL terminator)
#define TZ_TEXT_SIZE (4)  // max size of the "alternate timezone" complication text (including NULL terminator)

/** Macro table defining all config data.

    - Conftype is the last word in one of the RECEIVE_CONFIG_ options in config.c
    - CType is the C-side storage type
    - arrsuffix is the optional array indicator with length for the struct definition, e.g. [3]
    - Message key must match config.json and package.json, and be a valid C identifier.
    - Default value should match config.json.
*/
#define X_CONFIG_OPTIONS(MACRO) \
    /* conftype, C type, arrsuffix,      message key,       default */ \
    MACRO(COLOR, GColor,,                bgColor,             GColorWhite) \
    MACRO(COLOR, GColor,,                fgColor,             GColorBlack) \
    MACRO(COLOR, GColor,,                hourColor,           GColorBlack) \
    MACRO(COLOR, GColor,,                minuteColor,         GColorBlack) \
    MACRO(COLOR, GColor,,                comp1FgColor,        GColorPurple) \
    MACRO(COLOR, GColor,,                comp2FgColor,        GColorPurple) \
    MACRO(COLOR, GColor,,                comp3FgColor,        GColorPurple) \
    MACRO(COLOR, GColor,,                comp1BgColor,        GColorGreen) \
    MACRO(COLOR, GColor,,                comp2BgColor,        GColorGreen) \
    MACRO(COLOR, GColor,,                comp3BgColor,        GColorGreen) \
    MACRO(ENUM,  ComplicationType,,      comp1Type,           ComplicationType_Tick) \
    MACRO(ENUM,  ComplicationType,,      comp2Type,           ComplicationType_Tick) \
    MACRO(ENUM,  ComplicationType,,      comp3Type,           ComplicationType_Tick) \
    MACRO(ENUM,  ComplicationMode,,      comp1Mode,           ComplicationMode_OnActivate) \
    MACRO(ENUM,  ComplicationMode,,      comp2Mode,           ComplicationMode_OnActivate) \
    MACRO(ENUM,  ComplicationMode,,      comp3Mode,           ComplicationMode_OnActivate) \
    MACRO(CHAR,  int8_t,,                comp1Value,          0) \
    MACRO(CHAR,  int8_t,,                comp2Value,          0) \
    MACRO(CHAR,  int8_t,,                comp3Value,          0) \
    MACRO(STR,   char, [TZ_TEXT_SIZE],   comp1TimezoneText,   "alt") \
    MACRO(STR,   char, [TZ_TEXT_SIZE],   comp2TimezoneText,   "alt") \
    MACRO(STR,   char, [TZ_TEXT_SIZE],   comp3TimezoneText,   "alt") \
    MACRO(ENUM,  AnimationSpeed,,        animationSpeed,      AnimationSpeed_Normal) \
    MACRO(BOOL,  bool,,                  batteryMeterEnabled, false) \
    MACRO(INT,   uint8_t,,               batteryLowPercent,   20) \
    MACRO(COLOR, GColor,,                batteryLowColor,     GColorRed) \
    MACRO(BOOL,  bool,,                  faceTextEnabled,     false) \
    MACRO(COLOR, GColor,,                faceTextColor,       GColorLightGray) \
    MACRO(STR,   char, [FACE_TEXT_SIZE], faceText,            "pebble") \
/* end of X_CONFIG_OPTIONS */


typedef enum ComplicationType {
    ComplicationType_Tick = 0,
    ComplicationType_Battery = 1,
    ComplicationType_DayName = 2,
    ComplicationType_DayNum = 3,
    ComplicationType_MonthName = 4,
    ComplicationType_MonthNum = 5,
    ComplicationType_Steps = 6,  // value = isDistance
    ComplicationType_Sleep = 7, // value = isRestful
    ComplicationType_Timezone = 8,  // value = offsetHours
    // TODO temperature
    // TODO weather?
    // TODO bluetooth?
    // Heartrate not supported because PR2 doesn't have one
} ComplicationType;

typedef enum ComplicationMode {
    ComplicationMode_Off = 0,
    ComplicationMode_OnActivate = 1,
    ComplicationMode_OnAlways = 2,
} ComplicationMode;

typedef enum AnimationSpeed {
    AnimationSpeed_Normal = 0,
    AnimationSpeed_Fast = 1,
    AnimationSpeed_Off = 2,
} AnimationSpeed;
