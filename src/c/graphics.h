// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* Custom graphics API extensions. */

#pragma once

#include <pebble.h>

#include "debug.h"
#include "macros.h"


/** Bitfield values for graphics_draw_bitmap_in_rect_fliprot()'s op_flags.
    Flips are applied before Rot90.
*/
typedef enum OpFlag {
    OpFlag_None     = 0,
    OpFlag_FlipHorz = (1 << 0),
    OpFlag_FlipVert = (1 << 1),
    OpFlag_Rot90    = (1 << 2),
    // multi-op aliases
    OpFlag_Rot180   = ((1 << 0) | (1 << 1)),
    OpFlag_Rot270   = ((1 << 0) | (1 << 1) | (1 << 2))
} OpFlag;

void graphics_draw_bitmap_in_rect_fliprot(GContext *ctx, const GBitmap *src_bitmap, const GRect rect_ref, uint8_t op_flags);
