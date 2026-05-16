// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#include "debug.h"
#include "macros.h"


/** Bitfield values representing pixel-perfect flip (i.e. mirror) and rotation
    operations to perform when drawing a bitmap.

    Flips are applied before Rot90.
    Applying both flips is equivalent to rotating by 180.
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

/// A bitfield of OpFlag
typedef uint8_t FliprotFlags;


void fliprot_draw_bitmap(GContext *ctx, const GBitmap *src_bitmap, const GRect rect_ref, FliprotFlags op_flags);
FliprotFlags fliprot_for_angle(int32_t angle, int32_t hands_range_degrees);
bool fliprot_is_flipped(FliprotFlags op_flags);
size_t fliprot_hand_bitmap_index(
    size_t num_hand_bitmaps, int32_t hand_bitmaps_range_deg, int32_t angle, FliprotFlags* fliprot_flags);