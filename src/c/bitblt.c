/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/// Copied from PebbleOS\fw\applib\graphics\8_bit\bitblt_private.c

#include "bitblt.h"

#include <pebble.h>

#include "Macros.h"


// private helpers from PebbleOs/src/fw/util/graphics.h
static inline GPoint gpoint_add(const GPoint a, const GPoint b) {
    return GPoint(a.x + b.x, a.y + b.y);
}
static inline GPoint gpoint_sub(const GPoint a, const GPoint b) {
    return GPoint(a.x - b.x, a.y - b.y);
}
static inline int16_t grect_get_max_y(const GRect *rect) {
    return rect->origin.y + rect->size.h;
}
static inline int16_t grect_get_max_x(const GRect *rect) {
    return rect->origin.x + rect->size.w;
}


/******************************************************************************
 Public functions
******************************************************************************/

// Stripped out compositing_mode and tint_color options; GCompOpAssign only
void bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(GBitmap *dest_bitmap, const GBitmap *src_bitmap,
                                                  GRect dest_rect, GPoint src_origin_offset) {
    GRect src_bounds = gbitmap_get_bounds(src_bitmap);

    const int16_t dest_begin_y = dest_rect.origin.y;
    const int16_t dest_end_y = grect_get_max_y(&dest_rect);
    const int16_t src_begin_y = src_bounds.origin.y;
    const int16_t src_end_y = grect_get_max_y(&src_bounds);
    int16_t src_y = src_begin_y + src_origin_offset.y;

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
            dest[dest_x] = src[src_x];
        }
    }
}