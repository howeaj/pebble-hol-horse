// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** Translation bitmap effects. */

#include "translate.h"

#include <pebble.h>

#include "debug.h"
#include "macros.h"


/******************************************************************************
 Public functions
******************************************************************************/

/** Translate all pixels in `rect` by `vector`

    Limitations:
        - only supports negative vectors
        - doesn't clip within the framebuffer's bounds
        - only supports 8bit frame buffers (i.e. colour screens Basalt/Emery/Gabbro)
        - doesn't work on Chalk (packed GBitmapFormat8BitCircular framebuffer not supported)
        - sets the entire framebuffer dirty, which sucks but GETAFIX ignores dirty anyway
*/
void translate_rect(GContext* ctx, GRect rect, GVector vector) {
    GBitmap * const bitmap = graphics_capture_frame_buffer(ctx);
    uint8_t* const data = gbitmap_get_data(bitmap);
    // gabbro's framebuffer format is set to circular despite it actually being rect
    // which causes gbitmap_get_bytes_per_row to return 0 instead of the real value
    const int16_t bytes_per_row = PBL_DISPLAY_WIDTH; // gbitmap_get_bytes_per_row(dest_bitmap);

    // TODO support negative vectors by inverting the iteration
    const int16_t y_start = rect.origin.y;
    const int16_t y_end = rect.origin.y + rect.size.h;
    const int16_t x_start = rect.origin.x;
    const int16_t x_end = rect.origin.x + rect.size.w;

    for (int16_t y = y_start; y < y_end; y++) {
        for (int16_t x = x_start; x < x_end; x++) {
            const uint8_t* const src = &data[(y * bytes_per_row) + x];
            uint8_t* const dst = &data[((y + vector.dy) * bytes_per_row) + (x + vector.dx)];
            *dst = *src;
        }
    }

    // API graphics_release_frame_buffer sets the entire framebuffer dirty instead of letting you control it
    // ... but GETAFIX (PR2) display driver ignores the dirty rect anyway (see compositor_display_update())
    graphics_release_frame_buffer(ctx, bitmap);
}
