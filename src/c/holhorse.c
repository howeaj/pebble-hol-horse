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
#include "graphics.h"
#include "macros.h"

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

#define MPHI (12) // minutes per hour increment

// Latest angles for clock hands, in TRIGANGLE units
typedef struct HandAngles {
    int32_t hour;
    int32_t min;
    int32_t sec;
} HandAngles;
static HandAngles s_hand_angles;

// Update hand angles from a tick handler
static void analogue_update_angles(struct tm *tick_time, TimeUnits units_changed) {
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


/******************************************************************************
 This watchface implementation
******************************************************************************/

static GBitmap* s_face_real;
static GBitmap* s_hand_hour_45;

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
    graphics_context_set_antialiased(ctx, false);
    graphics_draw_line(ctx, centre, tip);
    if (analogue_is_vertical(angle)){
        graphics_draw_line(ctx, (GPoint){centre.x + 1, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 1, centre.y}, tip);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_antialiased(ctx, true);
        graphics_draw_line(ctx, (GPoint){centre.x + 2, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 2, centre.y}, tip);
    } else {  // horizontal
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y + 1}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y - 1}, tip);
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_context_set_antialiased(ctx, true);
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

    // hands
    const HandAngles hand_angles = analogue_get_angles();
    draw_hand_min(layer, ctx, hand_angles.min);
    draw_hand_hour(layer, ctx, hand_angles.hour);
    draw_hand_sec(layer, ctx, hand_angles.sec);
    // for (int i /*= 0; i < 60; i+= 4) {
        // hand_angles.sec = DEG_TO_TRIGANGLE(i * (360/60));
    // }

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
