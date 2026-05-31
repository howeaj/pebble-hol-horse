// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/** Fast pixel-perfect flip/rotation while drawing.

    This enables very fast flip or rotation of a bitmap during the draw,
    but is limited in the possible angles of rotation.
    Bitmaps can be rotated pixel-perfectly by multiples of 90 degrees.
    Every-other multiple of 45 can be achieved at the expense of also flipping the bitmap.

    If you want to have pixel-perfect clock hands (i.e. not automatically rotated/antialiased),
    this minimises the number of clock hand bitmap resources you need to provide to cover an entire clockface.
    If the hands are symmetrical, you only need 45 degrees-worth. If not, you need 90 degrees-worth.
*/

#include "fliprot.h"

#include <pebble.h>

#include "debug.h"
#include "macros.h"


/******************************************************************************
 Private functions
******************************************************************************/

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

// mirror val across the left edge of pivot
// 5, 5 -> 4
// 4, 5 -> 5
// 3, 5 -> 6
// 6, 5 -> 3
static inline int16_t mirror_across_pivot_edge(int16_t val, int16_t pivot) {
    return pivot + pivot - val - 1;
}

// TODO centre-pivot not yet supported
// mirror val across the centre of pivot
// 5, 5 -> 5
// 4, 5 -> 6
// 3, 5 -> 7
// 7, 5 -> 3
// static inline int16_t mirror_across_pivot_centre(int16_t val, int16_t pivot) {
//     return pivot + pivot - val;
// }

/// Blit a src bitmap onto dest with additional flips/rotations applied.
/// Based on PebbleOs/src/fw/applib/graphics' bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit()
static void bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_fliprot(
    GBitmap *dest_bitmap, const GBitmap *src_bitmap, GRect dest_rect, GPoint src_origin_offset, FliprotFlags flags)
{
    // TODO turn these into args; currently they just rotate around the centre
    // of dest_bitmap, which is assumed to be the display framebuffer.
    const bool is_corner_pivot = true;  // true  = pivot around the origin (top-left) corner of the pixel at `pivot`
                                        // false = pivot around the centre of the pixel at `pivot`
    const GPoint pivot = {PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_HEIGHT / 2};

    const GRect src_bounds = gbitmap_get_bounds(src_bitmap);
    const int16_t dest_begin_y = dest_rect.origin.y;
    const int16_t dest_end_y = grect_get_max_y(&dest_rect);
    const int16_t src_begin_y = src_bounds.origin.y;
    int16_t src_y = src_begin_y + src_origin_offset.y;

    // gabbro's framebuffer format is set to circular despite it actually being rect
    // which causes gbitmap_get_bytes_per_row to return 0 instead of the real value
    const int16_t dest_bytes_per_row = PBL_DISPLAY_WIDTH; // gbitmap_get_bytes_per_row(dest_bitmap);
    ASSERT(dest_bytes_per_row != 0);
    uint8_t* const dest = gbitmap_get_data(dest_bitmap);
    const GRect dest_bounds = gbitmap_get_bounds(dest_bitmap);

    const int16_t dest_delta_begin_x = MAX(0 - dest_rect.origin.x, 0);
    const int16_t dest_begin_x = dest_delta_begin_x ? 0 : dest_rect.origin.x;
    const int16_t dest_end_x = MIN(grect_get_max_x(&dest_rect), grect_get_max_x(&dest_bounds));

    uint8_t* const src = gbitmap_get_data(src_bitmap);
    // This is the initial position that takes into account destination delta shift
    const int16_t src_initial_x = src_bounds.origin.x + dest_delta_begin_x;
    const int16_t src_begin_x = src_bounds.origin.x;
    const int16_t src_end_x = grect_get_max_x(&src_bounds);
    const int32_t src_bytes_per_row = src_bounds.size.w;

    // precalculations for the OpFlag_Rot90 index calculation
    const int16_t rot_y_offset = is_corner_pivot ? 1 : 0;
    const int16_t index_x_rot_offset = pivot.y - pivot.x;
    const int16_t index_y_rot_offset = pivot.y + pivot.x - rot_y_offset;
    // The index is calculated as follows:
    // const GPoint relative_pos = {dest_x - pivot.x, dest_y - pivot.y};
    // const GPoint new_relative_pos = {-(relative_pos.y + rot_y_offset), relative_pos.x};
    // const GPoint new_abs_pos = {new_relative_pos.x + pivot.x, new_relative_pos.y + pivot.y};
    // index = (new_abs_pos.y * dest_bytes_per_row) + new_abs_pos.x;
    // which simplifies to:
    // index = ((dest_x + index_x_rot_offset) * dest_bytes_per_row) - dest_y + index_y_rot_offset;

    // x loop variables
    const int16_t dest_incr_x = (flags & OpFlag_FlipHorz) ? -1 : 1;
    const int32_t dest_start_x = ((flags & OpFlag_FlipHorz) ? mirror_across_pivot_edge(dest_begin_x, pivot.x)
                                                               : dest_begin_x);
    const int32_t dest_finish_x = ((flags & OpFlag_FlipHorz) ? mirror_across_pivot_edge(dest_end_x, pivot.x)
                                                                : dest_end_x);

    // foreach destination row
    for (int16_t i = 0; i < (dest_end_y - dest_begin_y); ++i, ++src_y) {
        const int16_t dest_y = (flags & OpFlag_FlipVert) ? mirror_across_pivot_edge(dest_begin_y + i, pivot.y)
                                                            : dest_begin_y + i;
        const int32_t src_y_offset = src_y * src_bytes_per_row;
        const int32_t dest_y_offset = dest_y * dest_bytes_per_row;
        // y-wrap handling removed

        // foreach destination column
        int32_t src_x = src_initial_x + src_origin_offset.x;
        int32_t dest_x = dest_start_x;
        while (dest_x != dest_finish_x) {
            if (!WITHIN(src_x, src_begin_x, src_end_x - 1)) {
                break;  // x-wrap handling removed
            }
            const GColor src_color = *(GColor8 *) &src[src_y_offset + src_x];

            // alpha blending removed; its either transparent or opaque
            if (src_color.a > 1) {
                int32_t index;
                if (flags & OpFlag_Rot90) {
                    index = ((dest_x + index_x_rot_offset) * dest_bytes_per_row) - dest_y + index_y_rot_offset;
                } else {
                    index = dest_y_offset + dest_x;
                }
                ASSERT(index < (PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT));
                dest[index] = src_color.argb;
            }

            dest_x += dest_incr_x;
            ++src_x;
        }
        // TODO is this method of looping actually faster?
        // for (int32_t j = 0; j < (dest_end_x - dest_begin_x); ++j, ++src_x) {
        //     const int32_t dest_x = (flags & OpFlag_FlipHorz) ? dest_end_x - j : dest_begin_x + j;
        // }
    }
}


/******************************************************************************
 Public functions
******************************************************************************/

/** Draw a bitmap with additional flips/rotations applied.

    Note the flips/rotations can transform the target draw area rect_ref,
    so this function can draw outside of the given rect_ref.

    Current limitations TODO:
        - only supports 8bit GBitmaps (i.e. "memoryFormat": "8Bit" in package.json)
        - only supports 8bit frame buffers (i.e. colour screens Basalt/Emery/Gabbro)
        - all FliprotFlags are applied pivoted around the centre of the display
        - doesn't account for rotating the bitmap off the edge of the screen
        - doesn't "tile" src_bitmap if rect_ref is bigger than src_bitmap's bounds
        - no alpha blending; treats every pixel as fully transparent or opaque
        - doesn't work on Chalk (packed GBitmapFormat8BitCircular framebuffer not supported)

    Based on PebbleOs/src/fw/applib/graphics' graphics_draw_bitmap_in_rect()
*/
void fliprot_draw_bitmap(
        GContext *ctx, const GBitmap *src_bitmap, const GRect rect_ref, FliprotFlags flags) {
    ASSERT(ctx != NULL);
    ASSERT(src_bitmap != NULL);

    GBitmap * const dest_bitmap = graphics_capture_frame_buffer(ctx);
    const GRect dest_bounds = gbitmap_get_bounds(dest_bitmap);

    // Make a copy of the rect and translate it to global screen coordinates
    GRect rect = rect_ref;
    rect.origin = gpoint_add(rect.origin, dest_bounds.origin);

    // Save the original origin to compensate the position within src when rect.origin is negative
    const GPoint unclipped_origin = rect.origin;

    // Clip the rect to avoid drawing outside of the bitmap memory
    grect_standardize(&rect);
    grect_clip(&rect, &dest_bounds);
    if (grect_is_empty(&rect)) {
        LOG("BAD RECT: origin %u, %u, size %u, %u",
            rect_ref.origin.x, rect_ref.origin.y, rect_ref.size.w, rect_ref.size.h);
        ASSERT(false);
    } else {
        // Calculate the offset of src_bitmap to use
        const GPoint src_offset = gpoint_sub(rect.origin, unclipped_origin);

        bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_fliprot(dest_bitmap, src_bitmap, rect, src_offset, flags);

        // removed "mark the region where the bitmap was drawn as dirty"
    }

    graphics_release_frame_buffer(ctx, dest_bitmap);
}

/** Return the FliprotFlags bitfield required to rotate a set of hands by `angle` degrees.
    The original set of hands range from 0 to `original_range_degrees`
        - this should be 45, 90 or 180.
    If hands_range_degrees is 45, every other segment will be flipped as well as rotated.
*/
FliprotFlags fliprot_for_angle(int32_t angle, int32_t original_range_degrees) {
    FliprotFlags flags;
    switch(angle / DEG_TO_TRIGANGLE(original_range_degrees)) {
    case 7:
        if (original_range_degrees < 90) {
            flags = OpFlag_FlipHorz;
            break;
        }
        // fallthrough
    case 6:
        if (original_range_degrees < 180) {
            flags = OpFlag_Rot270;
            break;
        }
        // fallthrough
    case 5:
        if (original_range_degrees < 90) {
            flags = OpFlag_FlipVert | OpFlag_Rot90;
            break;
        }
        // fallthrough
    case 4:
        flags = OpFlag_Rot180;
        break;
    case 3:
        if (original_range_degrees < 90) {
            flags = OpFlag_FlipVert;
            break;
        }
        // fallthrough
    case 2:
        if (original_range_degrees < 180) {
            flags = OpFlag_Rot90;
            break;
        }
        // fallthrough
    case 1:
        if (original_range_degrees < 90) {
            flags = OpFlag_FlipHorz | OpFlag_Rot90;
            break;
        }
        // fallthrough
    case 0:
        flags = OpFlag_None;
        break;
    default:
        ASSERT(false);  // TODO this triggered at 00:59
        LOG_ERR("%d", angle);
        flags = OpFlag_None;
        break;
    }
    return flags;
}

// Return true if the given FliprotFlags results in the bitmap being flipped instead of only rotated.
// This is the case for rotations which aren't a multiple of 90 degrees.
bool fliprot_is_flipped(FliprotFlags flags) {
    return ((flags & OpFlag_FlipHorz) == 0) != ((flags & OpFlag_FlipVert) == 0);
}

// Return the index and fliprot flags of the hand bitmap to use for `angle`
size_t fliprot_hand_bitmap_index(
    size_t num_hand_bitmaps, int32_t hand_bitmaps_range_deg, int32_t angle_per_bitmap, int32_t angle, FliprotFlags* fliprot_flags)
{
    // Select which bitmap to use
    int32_t i;
    for (i = 1; i < (int32_t)num_hand_bitmaps; i++) {
        if ((angle % DEG_TO_TRIGANGLE(hand_bitmaps_range_deg)) <= (i * angle_per_bitmap)) {
            break;
        }
    }
    i--;
    *fliprot_flags = fliprot_for_angle(angle, hand_bitmaps_range_deg);
    if (fliprot_is_flipped(*fliprot_flags)) {
        i = num_hand_bitmaps - i - 1;
    }
    // LOG("%i Draw angle %i (%i) = index #%u", TRIGANGLE_TO_DEG(angle_per_bitmap), angle, TRIGANGLE_TO_DEG(angle), i);
    return i;
}