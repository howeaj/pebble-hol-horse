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

static inline GPoint gpoint_add(const GPoint a, const GPoint b) {
    return GPoint(a.x + b.x, a.y + b.y);
}
static inline int16_t grect_get_max_x(const GRect *rect) {
    return rect->origin.x + rect->size.w;
}
static inline int16_t grect_get_max_y(const GRect *rect) {
    return rect->origin.y + rect->size.h;
}

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

// copy of bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit()
// modified to only set ~25% of the pixels at random
static void dissolve_bitmap_into_bitmap_tiled_8bit_to_8bit(
    GBitmap *dest_bitmap, const GBitmap *src_bitmap, GRect dest_rect, GPoint src_origin_offset
) {
    GRect src_bounds = gbitmap_get_bounds(src_bitmap);

    const int16_t dest_begin_y = dest_rect.origin.y;
    const int16_t dest_end_y = grect_get_max_y(&dest_rect);
    const int16_t src_begin_y = src_bounds.origin.y;
    const int16_t src_end_y = grect_get_max_y(&src_bounds);
    int16_t src_y = src_begin_y + src_origin_offset.y;

    // TODO this would be easier with access to rand32()
    int rand_bits = 0;
    STATIC_ASSERT(((uint32_t)RAND_MAX) < UINT32_MAX); // the sign bit is discarded
    uint32_t rand_mask = ((uint32_t)RAND_MAX) + 1;

    for (int16_t dest_y = dest_begin_y; dest_y < dest_end_y; ++dest_y, ++src_y) {
        // Wrap-around source bitmap vertically
        if (src_y >= src_end_y) {
            src_y = src_begin_y;
        }

        const GBitmapDataRowInfo dest_row_info = gbitmap_get_data_row_info(dest_bitmap, dest_y);
        uint8_t *dest = dest_row_info.data;
        const int16_t dest_delta_begin_x = MAX(dest_row_info.min_x - dest_rect.origin.x, 0);
        const int16_t dest_begin_x = dest_delta_begin_x ? dest_row_info.min_x : dest_rect.origin.x;
        const int16_t dest_end_x = MIN(grect_get_max_x(&dest_rect), dest_row_info.max_x + 1);
        if (dest_end_x < dest_begin_x) {
            continue;
        }

        const GBitmapDataRowInfo src_row_info = gbitmap_get_data_row_info(src_bitmap, src_y);
        const uint8_t *src = src_row_info.data;
        // This is the initial position that takes into account destination delta shift
        const int16_t src_initial_x = src_bounds.origin.x + dest_delta_begin_x;
        const int16_t src_begin_x = MAX(src_row_info.min_x, src_bounds.origin.x);
        const int16_t src_end_x = MIN(grect_get_max_x(&src_bounds), src_row_info.max_x + 1);

        int16_t src_x = src_initial_x + src_origin_offset.x;
        for (int16_t dest_x = dest_begin_x; dest_x < dest_end_x; ++dest_x, ++src_x) {
            if (!WITHIN(src_x, src_begin_x, src_end_x - 1)) {
                // Check if content should wrap (under and over) for tiling
                if (!WITHIN(src_x, src_bounds.origin.x,
                            grect_get_max_x(&src_bounds) - 1)) {
                    // keep correct bounds alignment for circular when tiling
                    src_x = src_bounds.origin.x +
                        ((src_x - src_bounds.origin.x) % src_bounds.size.w);
                } else {
                    // Increment source but don't draw
                    continue;
                }
            }

            if (dest[dest_x] == src[src_x]) {
                continue;
            }

            if (rand_mask > (uint32_t)RAND_MAX) {
                // TODO could reduce the randomness, if rand is expensive
                rand_bits = rand() & rand(); // 25% chance per pixel
                rand_mask = 1;
            }
            if (((uint32_t)rand_bits & rand_mask) != 0) {
                dest[dest_x] = src[src_x];
            }
            rand_mask = rand_mask << 1;
        }
    }
}

/** Randomly set pixels of `bitmap`, starting from `bitmap_origin_offset`, into the framebuffer's `rect`.

    Limitations:
        - hard-coded 25% chance (a good rate of dissolution at 30fps)
        - assumes the framebuffer and bitmap are each either GBitmapFormat8Bit or GBitmapFormat8BitRound
        - doesn't clip rect within the framebuffer's bounds
        - sets the entire framebuffer dirty
*/
void dissolve_bitmap_into_rect(GContext* ctx, GRect rect, const GBitmap* bitmap, GPoint bitmap_origin_offset) {
    GBitmap * const framebuffer = graphics_capture_frame_buffer(ctx);

    dissolve_bitmap_into_bitmap_tiled_8bit_to_8bit(
        framebuffer,
        bitmap,
        rect,
        bitmap_origin_offset
    );

    graphics_release_frame_buffer(ctx, framebuffer);
}
