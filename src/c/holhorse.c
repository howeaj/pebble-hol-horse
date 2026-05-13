// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    - Tick SFX
    - second leaves behind shadow?
    - 1 minute fast
    - what should happen at noon?
    - complications (replace tick SFX)
        - bluetooth
        - battery
        - date
    - config:
        1 min fast
        Shake to activate
        Touch to activate
        Vibe on BT disconnect
        Low-power-mode under 20%
        Invert colours
*/

#include <pebble.h>

#include "debug.h"
#include "macros.h"

static GBitmap* s_face_real;
static GBitmap* s_hand_hour_45;

#if DEBUG
// Return milliseconds since the epoch
uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}
#endif // DEBUG


/******************************************************************************
 Analogue watch boilerplate
******************************************************************************/

// Latest angles for clock hands, in TRIGANGLE units
typedef struct HandAngles {
    int32_t hour;
    int32_t min;
    int32_t sec;
} HandAngles;
static HandAngles s_hand_angles;

// Update hand angles from a tick handler
static void analogue_update_angles(struct tm *tick_time, TimeUnits units_changed) {
    #define MPHI (12) // minutes per hour increment
    if ((units_changed & HOUR_UNIT) || ((units_changed & MINUTE_UNIT) && ((tick_time->tm_min % MPHI) == 0))) {
        s_hand_angles.hour = (
            (((tick_time->tm_hour % 12) * (MINUTES_PER_HOUR / MPHI)) + (tick_time->tm_min / MPHI))
            * (TRIG_MAX_ANGLE / ((MINUTES_PER_DAY / MPHI) / 2))
        );
    }
    if (units_changed & MINUTE_UNIT) {
        s_hand_angles.min = tick_time->tm_min * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR);
    }
    s_hand_angles.sec = tick_time->tm_sec * (TRIG_MAX_ANGLE / SECONDS_PER_MINUTE);
}

// Return the latest hand angles
static HandAngles analogue_get_angles(void) {
    return s_hand_angles;
}

// Return true if angle is closer to vertical than horizontal
static bool analogue_is_vertical(int32_t angle) {
    return (
        (angle < DEG_TO_TRIGANGLE(45))
        || WITHIN(angle, DEG_TO_TRIGANGLE(135), DEG_TO_TRIGANGLE(225))
        || (angle > DEG_TO_TRIGANGLE(315))
    );
}

// used to pass bimap info to get/set pixel accurately
// from https://github.com/ygalanter/pebble-effect-layer
typedef struct {
   GBitmap *bitmap;  // actual bitmap for Chalk raw manipulation
   uint8_t *bitmap_data;
   int bytes_per_row;
   GBitmapFormat bitmap_format;
}  BitmapInfo;

// get pixel color at given coordinates
// Based on get_pixel() from https://github.com/ygalanter/pebble-effect-layer
static uint8_t get_pixel(BitmapInfo bitmap_info, int y, int x) {
    if ((bitmap_info.bitmap_format == GBitmapFormat1Bit) || (bitmap_info.bitmap_format == GBitmapFormat1BitPalette)) {
        // for 1 bit bitmap on Aplite - shifting right to get bit
        // 1 means White color in 1bit bitmaps; 2 means Black
        return (
            (((bitmap_info.bitmap_data[(y * bitmap_info.bytes_per_row) + (x / 8)] >> (x % 8)) & 1) == 1)
            ? GColorWhiteARGB8 : GColorBlackARGB8
        );
    } else {  // otherwise (assuming GBitmapFormat8Bit) going byte-wise
// #ifndef PBL_PLATFORM_CHALK
        return bitmap_info.bitmap_data[(y * bitmap_info.bytes_per_row) + x];
// #else // PBL_PLATFORM_CHALK
//         GBitmapDataRowInfo info = gbitmap_get_data_row_info(bitmap_info.bitmap, y);
//         if ((x >= info.min_x) && (x <= info.max_x)) {
//             return info.data[x];
//         } else {
//             return 0xffff;
//         }
// #endif // PBL_PLATFORM_CHALK
    }
}

// set pixel color at given coordinates
// Based on set_pixel() from https://github.com/ygalanter/pebble-effect-layer
static void set_pixel(BitmapInfo bitmap_info, int y, int x, uint8_t color) {

    if ((bitmap_info.bitmap_format == GBitmapFormat1Bit) || (bitmap_info.bitmap_format == GBitmapFormat1BitPalette)) {
        // for 1 bit bitmap on Aplite  --- verify if it needs to be different
        // White Color means 1 in 1bit bitmaps; Black Means 0
        color = ((color == GColorWhiteARGB8) ? 1 : 0);
        bitmap_info.bitmap_data[y * bitmap_info.bytes_per_row + x / 8] ^= (
            (-color ^ bitmap_info.bitmap_data[(y * bitmap_info.bytes_per_row) + (x / 8)])
            & (1 << (x % 8))
        );

    } else { // otherwise (assuming GBitmapFormat8Bit) going byte-wise
// #ifndef PBL_PLATFORM_CHALK
        bitmap_info.bitmap_data[y*bitmap_info.bytes_per_row + x] = color;
// #else // PBL_PLATFORM_CHALK
//         GBitmapDataRowInfo info = gbitmap_get_data_row_info(bitmap_info.bitmap, y);
//         if ((x >= info.min_x) && (x <= info.max_x)) {
//             info.data[x] = color;
//         }
// #endif // !PBL_PLATFORM_CHALK
    }
}

// Rotate the given `bitmap` in-place by 90 degrees.
// Based on bitmap_rotate_90() from https://github.com/ygalanter/pebble-effect-layer
void bitmap_rotate_90(GBitmap* bitmap, bool clockwise){
    const GRect bounds = gbitmap_get_bounds(bitmap);

    BitmapInfo bitmap_info;
    bitmap_info.bitmap = bitmap;
    bitmap_info.bitmap_data = gbitmap_get_data(bitmap);
    bitmap_info.bytes_per_row = gbitmap_get_bytes_per_row(bitmap);
    bitmap_info.bitmap_format = gbitmap_get_format(bitmap);

    ASSERT(bitmap_info.bitmap_format == GBitmapFormat8Bit);
    LOG("bitmap format: %d", bitmap_info.bitmap_format);

    // The centre of the bitmap
    const uint8_t xCn = bounds.origin.x + (bounds.size.w / 2);
    const uint8_t yCn = bounds.origin.y + (bounds.size.h / 2);

    const uint8_t qtr = MIN(bounds.size.w, bounds.size.h) / 2;

    for (int c1 = 0; c1 < qtr; c1++) {
        for (int c2 = 1; c2 < qtr; c2++){
            uint8_t temp_pixel = get_pixel(bitmap_info, yCn + c1, xCn + c2);
            if (clockwise){
                set_pixel(bitmap_info, yCn + c1, xCn + c2, get_pixel(bitmap_info, yCn - c2, xCn + c1));
                set_pixel(bitmap_info, yCn - c2, xCn + c1, get_pixel(bitmap_info, yCn - c1, xCn - c2));
                set_pixel(bitmap_info, yCn - c1, xCn - c2, get_pixel(bitmap_info, yCn + c2, xCn - c1));
                set_pixel(bitmap_info, yCn + c2, xCn - c1, temp_pixel);
            } else {
                set_pixel(bitmap_info, yCn + c1, xCn + c2, get_pixel(bitmap_info, yCn + c2, xCn - c1));
                set_pixel(bitmap_info, yCn + c2, xCn - c1, get_pixel(bitmap_info, yCn - c1, xCn - c2));
                set_pixel(bitmap_info, yCn - c1, xCn - c2, get_pixel(bitmap_info, yCn - c2, xCn + c1));
                set_pixel(bitmap_info, yCn - c2, xCn + c1, temp_pixel);
            }
        }
    }
}

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


/// Blit a src bitmap onto dest with additional flips/rotations applied.
/// Based on PebbleOs/src/fw/applib/graphics' bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit()
static void bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_fliprot(
    GBitmap *dest_bitmap, const GBitmap *src_bitmap, GRect dest_rect, GPoint src_origin_offset, uint8_t op_flags)
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

    // loop variables
    const int16_t dest_incr_x = (op_flags & OpFlag_FlipHorz) ? -1 : 1;
    const int32_t dest_start_x = ((op_flags & OpFlag_FlipHorz) ? dest_end_x : dest_begin_x);
    const int32_t dest_finish_x = ((op_flags & OpFlag_FlipHorz) ? dest_begin_x : dest_end_x);

    // foreach destination row
    for (int16_t i = 0; i < (dest_end_y - dest_begin_y); ++i, ++src_y) {
        const int16_t dest_y = (op_flags & OpFlag_FlipVert) ? dest_end_y - i : dest_begin_y + i;
        // y-wrap handling removed

        // foreach destination column
        int32_t src_x = src_initial_x + src_origin_offset.x;
        int32_t dest_x = dest_start_x;
        while (dest_x != dest_finish_x) {
            if (!WITHIN(src_x, src_begin_x, src_end_x - 1)) {
                break;  // x-wrap handling removed
            }
            GColor src_color = *(GColor8 *) &src[(src_y * src_bytes_per_row) + src_x];

            // alpha blending removed; its either transparent or opaque
            if (src_color.a > 1) {
                int32_t index;
                if (op_flags & OpFlag_Rot90) {
                    index = ((dest_x + index_x_rot_offset) * dest_bytes_per_row) - dest_y + index_y_rot_offset;
                } else {
                    index = (dest_y * dest_bytes_per_row) + dest_x;
                }
                ASSERT(index < (PBL_DISPLAY_WIDTH * PBL_DISPLAY_HEIGHT));
                dest[index] = src_color.argb;
            }

            dest_x += dest_incr_x;
            ++src_x;
        }
        // TODO is this method of looping actually faster?
        // for (int32_t j = 0; j < (dest_end_x - dest_begin_x); ++j, ++src_x) {
        //     const int32_t dest_x = (op_flags & OpFlag_FlipHorz) ? dest_end_x - j : dest_begin_x + j;
        // }
    }
}

/** Draw a bitmap with additional flips/rotations applied.

    op_flags is a bitfield of OpFlags to apply

    Current limitations:
        - doesn't work on Chalk (packed GBitmapFormat8BitCircular framebuffer not supported)
        - only supports 8bit frame buffers (i.e. colour screens Basalt/Emery/Gabbro)
        - only supports 8bit GBitmaps (i.e. "memoryFormat": "8Bit" in package.json)
        - all OpFlags are applied pivoted around the centre of the display
        - doesn't "tile" src_bitmap if rect_ref is bigger than its bounds

    Based on PebbleOs/src/fw/applib/graphics' graphics_draw_bitmap_in_rect()
*/
void graphics_draw_bitmap_in_rect_fliprot(GContext *ctx, const GBitmap *src_bitmap, const GRect rect_ref, uint32_t op_flags) {
    if (!ctx || !src_bitmap) {
        ASSERT(false);
        return;
    }

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
        ASSERT(false);
        return;
    }

    // Calculate the offset of src_bitmap to use
    const GPoint src_offset = gpoint_sub(rect.origin, unclipped_origin);

    bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit_fliprot(dest_bitmap, src_bitmap, rect, src_offset, op_flags);

    // removed "mark the region where the bitmap was drawn as dirty"

    graphics_release_frame_buffer(ctx, dest_bitmap);
}


/******************************************************************************
 This watchface implementation
******************************************************************************/

static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
    analogue_update_angles(tick_time, units_changed);
}

static void draw_hand_min(Layer* layer, GContext* ctx, int32_t angle) {
    // const GRect bounds = layer_get_bounds(layer);
    // const GPoint centre = grect_center_point(&bounds);
    // const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) - MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 2, 5));
    // TODO
}

static void draw_hand_hour(Layer* layer, GContext* ctx, int32_t angle) {
    // const GRect bounds = layer_get_bounds(layer);
    // const GPoint centre = grect_center_point(&bounds);
    // TODO
    const GRect bounds = layer_get_bounds(layer);
    // graphics_draw_bitmap_in_rect_fliprot(ctx, s_hand_hour_45, bounds, 0);
    // graphics_draw_bitmap_in_rect_fliprot(ctx, s_hand_hour_45, bounds, OpFlag_FlipHorz);
    // graphics_draw_bitmap_in_rect_fliprot(ctx, s_hand_hour_45, bounds, OpFlag_FlipVert);
    // graphics_draw_bitmap_in_rect_fliprot(ctx, s_hand_hour_45, bounds, OpFlag_FlipHorz | OpFlag_FlipVert);
    graphics_draw_bitmap_in_rect_fliprot(ctx, s_hand_hour_45, bounds, OpFlag_FlipVert | OpFlag_Rot90);
}

static void draw_hand_sec(Layer* layer, GContext* ctx, int32_t angle) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint centre = grect_center_point(&bounds);
    const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) - MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 2, 5));
    const GPoint tip = gpoint_from_polar(mid_bounds, GOvalScaleModeFitCircle, angle);
    graphics_context_set_stroke_color(ctx, GColorWhite);

    graphics_draw_line(ctx, centre, tip);
    if (analogue_is_vertical(angle)){
        graphics_draw_line(ctx, (GPoint){centre.x + 1, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 1, centre.y}, tip);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_draw_line(ctx, (GPoint){centre.x + 2, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 2, centre.y}, tip);
    } else {
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y + 1}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y - 1}, tip);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y + 2}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y - 2}, tip);
    }
}

#define NUM_CLOCK_INDICES (60)
static void draw_root_layer(Layer* layer, GContext* ctx) {
    PROFILE_START();

    const GRect bounds = layer_get_bounds(layer);

    // TODO move this into the bitmap
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // static background
    graphics_context_set_compositing_mode(ctx, GCompOpSet); // enable transparency, TODO disable
    graphics_draw_bitmap_in_rect(ctx, s_face_real, bounds);

    // indices
    // TODO move this into the bitmap
    graphics_context_set_fill_color(ctx, GColorBlack);
    const GRect indices_bounds = grect_crop(bounds, PBL_DISPLAY_WIDTH / 6);
    for (int32_t i = 0; i < NUM_CLOCK_INDICES; i++) {
        const int16_t angle = i * DEG_TO_TRIGANGLE(360 / NUM_CLOCK_INDICES);
        const GPoint point = gpoint_from_polar(indices_bounds, GOvalScaleModeFitCircle, angle);
        graphics_fill_circle(ctx, point, (i % 5 == 0) ? 2 : 1);
    }

    // centre
    // TODO move this into the bitmap
    const GPoint centre = grect_center_point(&bounds);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, centre, 11);
    graphics_draw_circle(ctx, centre, 9);
    graphics_draw_circle(ctx, centre, 3);

    const HandAngles hand_angles = analogue_get_angles();

    // for (int i = 0; i < 60; i++)
    // {
        // hand_angles.min = DEG_TO_TRIGANGLE(i * (360/60));
    draw_hand_min(layer, ctx, hand_angles.min);
    draw_hand_hour(layer, ctx, hand_angles.hour);
    draw_hand_sec(layer, ctx, hand_angles.sec);

    PROFILE_END("draw_main_window_layer");
}

static void init_resources(Layer *root_layer) {
    s_face_real = gbitmap_create_with_resource(RESOURCE_ID_FACE_REAL);
    s_hand_hour_45 = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_45);
}

static void destroy_resources(void) {
    gbitmap_destroy(s_face_real);
    gbitmap_destroy(s_hand_hour_45);
}


/******************************************************************************
 Generic watchface boilerplate
******************************************************************************/

static Window* s_main_window;

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    handle_tick(tick_time, units_changed);
    layer_mark_dirty(window_get_root_layer(s_main_window));
}

static void main_window_load(Window *window) {
    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void main_window_unload(Window *window) {
    destroy_resources();
    tick_timer_service_unsubscribe();
}

static void init(void) {
    s_main_window = window_create();
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    const time_t now = time(NULL);
    tick_handler(localtime(&now),
        SECOND_UNIT | MINUTE_UNIT | HOUR_UNIT | DAY_UNIT | MONTH_UNIT | YEAR_UNIT);
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    DEMO_BACKLIGHT_ENABLE(true);

    init();
    app_event_loop();
    deinit();

    DEMO_BACKLIGHT_ENABLE(false);
}
