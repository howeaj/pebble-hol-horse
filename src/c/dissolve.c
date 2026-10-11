// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** Dissolve bitmap effects. */

#include "dissolve.h"

#include <pebble.h>

#include "debug.h"
#include "macros.h"



// todo don't dirty whole framebuffer

// copy GETAFIX value
// #define FRAMEBUFFER_SIZE_BYTES (PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT)

// typedef struct FrameBuffer {
//   uint8_t buffer[FRAMEBUFFER_SIZE_BYTES];
//   GSize size; //<! Active size of the framebuffer
//   GRect dirty_rect; //<! Smallest rect covering all dirty pixels.
//   bool is_dirty;
// } FrameBuffer;

// static void mark_framebuffer_dirty(FrameBuffer* framebuffer) {
//     framebuffer->is_dirty = false;
//     framebuffer->dirty_rect = (GRect){{0, 0}, {0, 0}};
// }


/******************************************************************************
 Public functions
******************************************************************************/

/// Return the number of bytes in the given GBitmap's data.
size_t gbitmap_get_size(const GBitmap *bitmap) {
    return (size_t)(gbitmap_get_bytes_per_row(bitmap) * gbitmap_get_bounds(bitmap).size.h);
}

/** Randomly set each pixel in `bitmap` to GColorClear, with a % chance per pixel.

    Limitations:
        - rand() only gives RAND_MAX-worth of bits (sign bit is always 0)
            and we ignore this fact, so actually 1/32 pixels are never cleared.
            this will look obvious/bad if the bitmap's width is 32, for example
        - hard-coded 25% chance (a good rate of dissolution at 30fps)
        - assumes GBitmapFormat1BitPalette
        - assumes palette = {0: nonclear, 1: clear}
*/
void dissolve_bitmap(GBitmap *bitmap) {
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

/** Randomly set each pixel in the framebuffer's `grect` to `color`, with a % chance per pixel.

    Limitations:
        - hard-coded 25% chance (a good rate of dissolution at 30fps)
        - doesn't clip rect within the framebuffer's bounds
        - only supports 8bit frame buffers (i.e. colour screens Basalt/Emery/Gabbro)
        - doesn't work on Chalk (packed GBitmapFormat8BitCircular framebuffer not supported)
        - sets the entire framebuffer dirty
*/
void dissolve_rect(GContext* ctx, GRect rect, GColor color) {
    GBitmap * const bitmap = graphics_capture_frame_buffer(ctx);
    uint8_t* const data = gbitmap_get_data(bitmap);
    // gabbro's framebuffer format is set to circular despite it actually being rect
    // which causes gbitmap_get_bytes_per_row to return 0 instead of the real value
    const int16_t bytes_per_row = PBL_DISPLAY_WIDTH; // gbitmap_get_bytes_per_row(dest_bitmap);

    const int16_t y_start = rect.origin.y;
    const int16_t y_end = rect.origin.y + rect.size.h;
    const int16_t x_start = rect.origin.x;
    const int16_t x_end = rect.origin.x + rect.size.w;

    // TODO this would be easier with access to rand32()
    int rand_bits = 0;
    STATIC_ASSERT(((uint32_t)RAND_MAX) < UINT32_MAX); // the sign bit is discarded
    uint32_t rand_mask = ((uint32_t)RAND_MAX) + 1;

    for (int16_t y = y_start; y < y_end; y++) {
        for (int16_t x = x_start; x < x_end; x++) {
            uint8_t* const pixel = &data[(y * bytes_per_row) + x];
            if (*pixel == color.argb) {
                continue;
            }
            if (rand_mask > (uint32_t)RAND_MAX) {
                // TODO could reduce the randomness, if rand is expensive
                rand_bits = rand() & rand(); // 25% chance per pixel
                rand_mask = 1;
            }
            if (((uint32_t)rand_bits & rand_mask) != 0) {
                *pixel = color.argb;
            }
            rand_mask = rand_mask << 1;
        }
    }

    // API graphics_release_frame_buffer sets the entire framebuffer dirty instead of letting you control it
    graphics_release_frame_buffer(ctx, bitmap);
}
