// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** Dissolve bitmap effect */

#include "dissolve.h"

#include <pebble.h>

#include "debug.h"
#include "macros.h"


/******************************************************************************
 Public functions
******************************************************************************/

/// Return the number of bytes in the given GBitmap's data.
size_t gbitmap_get_size(const GBitmap *bitmap) {
    return (size_t)(gbitmap_get_bytes_per_row(bitmap) * gbitmap_get_bounds(bitmap).size.h);
}

/** Randomly set each pixel in `bitmap` to GColorClear, with a % chance per pixel.

    Limitations:
        - hard-coded 25% chance (a good rate of dissolution at 30fps)
        - assumes GBitmapFormat1BitPalette
        - assumes palette = {0: nonclear, 1: clear}
        - no guaranteed number of steps to clear all pixels
*/
void dissolve(GBitmap *bitmap) {
    ASSERT(gbitmap_get_format(bitmap) == GBitmapFormat1BitPalette);

    uint8_t* const data = gbitmap_get_data(bitmap);
    const size_t data_size = gbitmap_get_size(bitmap);

    // assuming GBitmapFormat1BitPalette 0=nonclear, 1=clear
    for (size_t i = 0; i < (data_size / sizeof(int)); i++) {
        ((int*)data)[i] |= (rand() & rand());  // one rand = 50%, two rands = 25%
    }
    for (size_t i = sizeof(int) * (data_size / sizeof(int)); i < (data_size % sizeof(int)); i++) {
        const int mask = rand();
        data[i] |= ((mask & (mask >> 8)) & 0xff);
    }
}
