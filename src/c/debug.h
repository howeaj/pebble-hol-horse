// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

// Debug options. Everything should be false for release builds!

#pragma once

#include <pebble.h>

#define DEMO 0
#define DEBUG 0
#define FORCE_BACKLIGHT_ON (DEMO || 0)
#define FAST_TIME_MS 0  // millisecond per minute, or 0 to disable
#define FORCE_BLUETOOTH_DISCONNECT 0  // because emu-bt-connection takes forever
#define DEBUG_DRAW_FACE 0  // plot parts of the face with pebble graphics (normally baked in to face.png)
#define DEBUG_SHOW_COMPL_RECTS 0  // draw the complication placement bounding boxes
#define DEBUG_COMPL_VAL 0  // Fake complication value

#if DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (is_demo)
#else  // !DEMO
    #define IF_DEMO_ELSE(is_demo, not_demo) (not_demo)
#endif  // !DEMO

#if FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on) light_enable(on)
#else // !FORCE_BACKLIGHT_ON
    #define DEMO_BACKLIGHT_ENABLE(on)
#endif // !FORCE_BACKLIGHT_ON
