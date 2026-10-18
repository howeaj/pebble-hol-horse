// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    - fix broken positioning when "on activity" set at lower index than "always"
    - prevent "always" from covering the battery meter
    - animation improvements
        - don't dirty entire framebuffer
        - don't wakeup when nothing needs updates
    - complications avoid timeline peek
    - more colour themes
    - -flto gcc opt
    - emery support
    - Debug render all complication values for testing
    - If seconds are visible at noon, play shoot animation panels
    - turn battery meter green when on charge & 80%
    - config:
        1 min fast
        Activation options
            Shake
            Backlight
        Low-power-mode under 20%
        longer seconds display
        languages
*/

#include <pebble.h>

#include "analogue.h"
#include "complications.h"
#include "debug.h"
#include "fliprot.h"
#include "macros.h"
#include "misc.h"


static bool should_show_seconds(void);


/******************************************************************************
 This watchface implementation; hands
******************************************************************************/

/// Return a target GRect which places `point` of `bitmap` onto the centre of `layer`
static GRect bitmap_point_on_centre(const GBitmap* bitmap, const GPoint point, const Layer* layer) {
    const GRect layer_bounds = layer_get_bounds(layer);
    const GPoint layer_centre = grect_center_point(&layer_bounds);
    GRect target_rect = gbitmap_get_bounds(bitmap);
    target_rect.origin = gpoint_sub(layer_centre, point);
    return target_rect;
}

#define NUM_HOUR_BITMAPS (7)  // The number of unique hour hand bitmaps
#define HOUR_BITMAPS_RANGE_DEG (45)  // The range of degrees covered by our hour hand bitmaps
#define DEGREES_PER_HOUR_HAND DEG_TO_TRIGANGLE(7.5)  // the angle between each hour hand
static GBitmap* s_hour_hand_bitmaps[NUM_HOUR_BITMAPS];
static void draw_hand_hour(Layer* layer, GContext* ctx, int32_t angle) {
    FliprotFlags fliprot_flags;
    const size_t bitmap_index = fliprot_hand_bitmap_index(
        NUM_HOUR_BITMAPS,
        HOUR_BITMAPS_RANGE_DEG,
        DEGREES_PER_HOUR_HAND,
        (angle + (int32_t)(DEGREES_PER_HOUR_HAND / 2)) % TRIG_MAX_ANGLE,  // offset input angle to minimise final display error
        &fliprot_flags
    );

    static const GPoint bitmap_centre_points[NUM_HOUR_BITMAPS] = {
        {9, 79},  // 0
        {5, 77},  // 7.5
        {3, 74},  // 15
        {2, 71},  // 22.5
        {0, 67},  // 30
        {0, 61},  // 37.5
        {0, 54},  // 45
    };
    const GBitmap* bitmap = s_hour_hand_bitmaps[bitmap_index];
    const GRect target_rect = bitmap_point_on_centre(bitmap, bitmap_centre_points[bitmap_index], layer);
    fliprot_draw_bitmap(ctx, bitmap, target_rect, fliprot_flags);
}

#define NUM_MINUTE_BITMAPS (8)  // The number of unique minute hand bitmaps
#define MINUTE_BITMAPS_RANGE_DEG (45)  // The range of degrees covered by our minute hand bitmaps
#define DEGREES_PER_MINUTE_HAND DEG_TO_TRIGANGLE(360 / MINUTES_PER_HOUR)
static GBitmap* s_min_hand_bitmaps[NUM_MINUTE_BITMAPS];
static void draw_hand_min(Layer* layer, GContext* ctx, int32_t angle) {
    FliprotFlags fliprot_flags;
    const size_t bitmap_index = fliprot_hand_bitmap_index(
        NUM_MINUTE_BITMAPS, MINUTE_BITMAPS_RANGE_DEG, DEGREES_PER_MINUTE_HAND, angle, &fliprot_flags);

    // Select the target draw origin so that its centrepoint is in the centre of the screen.
    static const GPoint bitmap_centre_points[NUM_MINUTE_BITMAPS] = {
        {8, 122},  // 0
        {4, 121},  // 6
        {2, 119},  // 12
        {1, 116},  // 18
        {0, 112},  // 24
        {0, 105},  // 30
        {0, 99},  // 36
        {0, 91},  // 42
    };
    const GBitmap* bitmap = s_min_hand_bitmaps[bitmap_index];
    const GRect target_rect = bitmap_point_on_centre(bitmap, bitmap_centre_points[bitmap_index], layer);
    fliprot_draw_bitmap(ctx, bitmap, target_rect, fliprot_flags);
}

static void draw_hand_sec(Layer* layer, GContext* ctx, int32_t angle) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint centre = grect_center_point(&bounds);
    const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) - MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 2, 5));
    const GPoint tip = gpoint_from_polar(mid_bounds, GOvalScaleModeFitCircle, angle);
    const Config* config = config_get();

    graphics_context_set_stroke_color(ctx, config->bgColor);
    graphics_context_set_antialiased(ctx, false);
    graphics_draw_line(ctx, centre, tip);
    if (analogue_is_vertical(angle)){
        graphics_draw_line(ctx, (GPoint){centre.x + 1, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 1, centre.y}, tip);
        graphics_context_set_stroke_color(ctx, config->secondColor);
        graphics_context_set_antialiased(ctx, true);
        graphics_draw_line(ctx, (GPoint){centre.x + 2, centre.y}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x - 2, centre.y}, tip);
    } else {  // horizontal
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y + 1}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y - 1}, tip);
        graphics_context_set_stroke_color(ctx, config->secondColor);
        graphics_context_set_antialiased(ctx, true);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y + 2}, tip);
        graphics_draw_line(ctx, (GPoint){centre.x, centre.y - 2}, tip);
    }
}

static void draw_hand_sec_shadow(Layer* layer, GContext* ctx, int32_t angle) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint centre = grect_center_point(&bounds);
    const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) + MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 1, 5));
    const GPoint tip = gpoint_from_polar(mid_bounds, GOvalScaleModeFitCircle, angle);

    graphics_context_set_stroke_color(ctx, config_get()->secondColor);
    graphics_draw_line(ctx, centre, tip);
}


/******************************************************************************
 This watchface implementation; handlers
******************************************************************************/

static GBitmap* s_face;
static GFont s_face_font;
static char s_face_text[FACE_TEXT_SIZE];
static GBitmap* s_icon_disconnect;
static GBitmap* s_icon_silence;

#define MAX_NUM_SECONDS (3)
static int8_t s_num_seconds_shown = 0;
static bool s_is_full_redraw = false;

static GColor s_battery_color = GColorRed;  // color of the battery meter
static uint8_t s_battery_pixels = 0;  // width of the battery meter


static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
    // TRACE("handle_tick; s_num_seconds_shown = %d", s_num_seconds_shown);
    analogue_update_angles(tick_time, units_changed);

    if (should_show_seconds()) {
        s_num_seconds_shown = MIN(MAX_NUM_SECONDS, s_num_seconds_shown + 1);
        ASSERT(s_num_seconds_shown > 0);
        complication_start_next();
    } else {
        if (units_changed & MINUTE_UNIT) {
            complications_update();
        }
        if (s_num_seconds_shown > 0) {
            complications_stop();
            s_num_seconds_shown = 0;
        }
    }

    mark_root_window_dirty(true);
}

static void handle_battery(BatteryChargeState charge) {
    const Config* config  = config_get();

    s_battery_color = (charge.charge_percent > config->batteryLowPercent) ? config->faceTextColor : config->batteryLowColor;

    const uint8_t char_width = 10;
    const int text_len = strlen(config->faceText);
    const uint8_t num_chars = (text_len == 0) ? 6 : MAX(3, text_len);
    const uint8_t max_width = num_chars * char_width;
    s_battery_pixels = MUL_FRACT(max_width, charge.charge_percent, 100);

    mark_root_window_dirty(true);
}

static void handle_connection(bool is_connected) {
    LOG("is_connected changed to %s", BOOL_TO_STR(is_connected));
    mark_root_window_dirty(true);  // TODO could avoid a full redraw (probably not worth it)
}

static void draw_disconnect_status(const GRect bounds, GContext* ctx) {
    UNUSED(bounds);
    if (!connection_service_peek_pebble_app_connection() || FORCE_BLUETOOTH_DISCONNECT) {
        const GRect rect = {
            .origin = {187, 33},
            .size = gbitmap_get_bounds(s_icon_disconnect).size,
        };
        graphics_draw_bitmap_in_rect(ctx, s_icon_disconnect, rect);
    }
}

static void draw_quiet_time_status(const GRect bounds, GContext* ctx) {
    UNUSED(bounds);
    if (quiet_time_is_active() || FORCE_QUIET_TIME) {
        const GRect rect = {
            .origin = {35, 43},  // note {9, 86} would be the next section down
            .size = gbitmap_get_bounds(s_icon_silence).size,
        };
        graphics_draw_bitmap_in_rect(ctx, s_icon_silence, rect);
    }
}

static void draw_battery_meter(const GRect bounds, GContext* ctx) {
    graphics_context_set_fill_color(ctx, s_battery_color);
    const GRect rect = {
        .origin = {(bounds.size.h - s_battery_pixels) / 2,
                    bounds.origin.y + (bounds.size.h / 4) + 20}, // below draw_face_text's text_bounds
        .size = {s_battery_pixels, 2},
    };
    graphics_fill_rect(ctx, rect, 0, GCornerNone);
}

static void draw_face_text(const GRect bounds, GContext* ctx) {
    const Config* config = config_get();
    graphics_context_set_text_color(ctx, config->faceTextColor);
    const GRect text_bounds = {
        .origin = {0, bounds.origin.y + (bounds.size.h / 4)},
        .size = {bounds.size.w, 18},
    };
    graphics_draw_text(ctx, s_face_text, s_face_font, text_bounds,
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

#if DEBUG_DRAW_FACE
// Draw parts of the face with pebble graphics commands.
// Note this doesn't exactly draw the baked face.png because pebble's circle gfx is imprecise.
static void draw_face(GRect bounds, GContext* ctx) {
    // indices
    graphics_context_set_fill_color(ctx, GColorBlack);
    const GRect indices_bounds = grect_crop(bounds, PBL_DISPLAY_WIDTH / 6);
    for (int32_t i = 0; i < MINUTES_PER_HOUR; i++) {
        const int16_t angle = i * DEG_TO_TRIGANGLE(360 / MINUTES_PER_HOUR);
        const GPoint point = gpoint_from_polar(indices_bounds, GOvalScaleModeFitCircle, angle);
        graphics_fill_circle(ctx, point, (i % 5 == 0) ? 2 : 1);
        // just single pixels
        // graphics_fill_radial(ctx, (GRect){point, {(i % 5 == 0) ? 1 : 1, 0}},
        //     GOvalScaleModeFillCircle, 5, 0, DEG_TO_TRIGANGLE(360));
    }

    // centre
    const GPoint centre = grect_center_point(&bounds);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, centre, 11);
    graphics_draw_circle(ctx, centre, 9);
    graphics_draw_circle(ctx, centre, 3);
}
#endif // DEBUG_DRAW_FACE

static void draw_root_layer(Layer* layer, GContext* ctx) {
    // TRACE("draw_root_layer; s_num_seconds_shown = %d", s_num_seconds_shown);
    PROFILE_START();

    graphics_context_set_compositing_mode(ctx, GCompOpSet); // enable transparency
    if (s_is_full_redraw)
    {
        const GRect bounds = layer_get_bounds(layer);
        const Config* config = config_get();

#if DEBUG_DRAW_FACE
        graphics_context_set_fill_color(ctx, config->bgColor);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif // DEBUG_DRAW_FACE

        // baked face bitmap
        graphics_draw_bitmap_in_rect(ctx, s_face, bounds);
#if DEBUG_DRAW_FACE
        draw_face(bounds, ctx);
#endif // DEBUG_DRAW_FACE

        if (config->connStatusEnabled) {
            draw_disconnect_status(bounds, ctx);
        }
        if (config->quietStatusEnabled) {
            draw_quiet_time_status(bounds, ctx);
        }
        if (config->batteryMeterEnabled) {
            draw_face_text(bounds, ctx);
            draw_battery_meter(bounds, ctx);
        }

        // hands
        const HandAngles hand_angles = analogue_get_angles();
        draw_hand_min(layer, ctx, hand_angles.min);
        draw_hand_hour(layer, ctx, hand_angles.hour);

        for (int8_t i = 1; i < s_num_seconds_shown; i++) {
            draw_hand_sec_shadow(layer, ctx, hand_angles.sec - (i * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR)));
        }
        if (s_num_seconds_shown > 0) {
            draw_hand_sec(layer, ctx, hand_angles.sec);
        }

        // draw all hand positions, for debugging
        // for (int32_t i = 0; i < 60 ; i++) {
        //     draw_hand_hour(layer, ctx, DEG_TO_TRIGANGLE(i * (360 / 60)));
        // }
    }

    complications_draw(layer, ctx, s_is_full_redraw);

    s_is_full_redraw = false;

    PROFILE_END("draw_root_layer");
}

static void set_bitmap_colors(const Config* config) {
    const GColor fgColor = config->fgColor;
    const GColor bgColor = config->bgColor;

    gbitmap_set_color(s_face, 0, bgColor);
    gbitmap_set_color(s_face, 1, fgColor);
    gbitmap_set_color(s_face, 2, (bgColor.argb == GColorBlack.argb) ? GColorDarkGray : GColorLightGray);
    gbitmap_set_color(s_face, 3, config->minuteColor);
    gbitmap_set_color(s_face, 4, config->hourColor);

    gbitmap_set_color(s_icon_disconnect, 0, config->status1Color);
    gbitmap_set_color(s_icon_silence, 0, config->status2Color);

    for (size_t i = 0; i < ARRAY_LEN(s_hour_hand_bitmaps); i++) {
        gbitmap_set_color(s_hour_hand_bitmaps[i], 0, config->hourColor);
        gbitmap_set_color(s_hour_hand_bitmaps[i], 3, bgColor);
    }
    for (size_t i = 0; i < ARRAY_LEN(s_min_hand_bitmaps); i++) {
        gbitmap_set_color(s_min_hand_bitmaps[i], 0, config->minuteColor);
        gbitmap_set_color(s_min_hand_bitmaps[i], 3, bgColor);
    }
}

static void init_resources(Layer *root_layer) {
    complications_init();
    s_face = gbitmap_create_with_resource(RESOURCE_ID_FACE_REAL);
    s_icon_disconnect = gbitmap_create_with_resource(RESOURCE_ID_MENACING);
    s_icon_silence = gbitmap_create_with_resource(RESOURCE_ID_SILENCE);
    s_face_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_SERIF_18));
    s_hour_hand_bitmaps[0] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_0);
    s_hour_hand_bitmaps[1] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_7_5);
    s_hour_hand_bitmaps[2] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_15);
    s_hour_hand_bitmaps[3] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_22_5);
    s_hour_hand_bitmaps[4] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_30);
    s_hour_hand_bitmaps[5] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_37_5);
    s_hour_hand_bitmaps[6] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_45);
    s_min_hand_bitmaps[0] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_0);
    s_min_hand_bitmaps[1] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_1);
    s_min_hand_bitmaps[2] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_2);
    s_min_hand_bitmaps[3] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_3);
    s_min_hand_bitmaps[4] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_4);
    s_min_hand_bitmaps[5] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_5);
    s_min_hand_bitmaps[6] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_6);
    s_min_hand_bitmaps[7] = gbitmap_create_with_resource(RESOURCE_ID_HAND_MIN_7);
}

static void destroy_resources(void) {
    connection_service_unsubscribe();
    battery_state_service_unsubscribe();
    complications_destroy();
    gbitmap_destroy(s_face);
    gbitmap_destroy(s_icon_disconnect);
    gbitmap_destroy(s_icon_silence);
    fonts_unload_custom_font(s_face_font);
    for (size_t i = 0; i < ARRAY_LEN(s_hour_hand_bitmaps); i++) {
        gbitmap_destroy(s_hour_hand_bitmaps[i]);
    }
    for (size_t i = 0; i < ARRAY_LEN(s_min_hand_bitmaps); i++) {
        gbitmap_destroy(s_min_hand_bitmaps[i]);
    }
}

static void handle_config(const Config* config) {
    set_bitmap_colors(config);
    complications_configure(config);

    strscpy(s_face_text, config->faceText, FACE_TEXT_SIZE);
    string_to_lower(s_face_text, FACE_TEXT_SIZE);

    if (config->batteryMeterEnabled) {
        handle_battery(battery_state_service_peek());  // depends on s_face_text
        battery_state_service_subscribe(handle_battery);
    } else {
        battery_state_service_unsubscribe();
    }
}


/******************************************************************************
 Generic watchface boilerplate
******************************************************************************/

#define ACTIVE_DURATION_S (3)

static Window* s_main_window;
static int8_t s_active_ticks_remaining = -1;

static bool should_show_seconds(void) {
    return s_active_ticks_remaining > 0;
}

void mark_root_window_dirty(bool full_redraw) {
    layer_mark_dirty(window_get_root_layer(s_main_window));
    if (full_redraw) {
        s_is_full_redraw = true;
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    // TRACE("tick_handler; s_active_ticks_remaining = %d", s_active_ticks_remaining);
    handle_tick(tick_time, units_changed);

    if (s_active_ticks_remaining == 0) {
        LOG("Schedule minute updates");
#if !FAST_TIME_MS
        tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
#endif // !FAST_TIME_MS
    }
    if (s_active_ticks_remaining >= 0) {
        s_active_ticks_remaining -= 1;
    }
}

static void activation_handler(void* context) {
    UNUSED(context);
    if (s_active_ticks_remaining == -1) {
        LOG("Schedule second updates");
#if !FAST_TIME_MS
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
#endif // !FAST_TIME_MS
        s_active_ticks_remaining = ACTIVE_DURATION_S;
    }
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    activation_handler(NULL);
}

#if FAST_TIME_MS
time_t s_fake_time = 0;
static void fast_time_handler(void* context) {
    s_fake_time += SECONDS_PER_MINUTE;
    tick_handler(localtime(&s_fake_time), SECOND_UNIT | MINUTE_UNIT | (((s_fake_time % 60) == 0) ? HOUR_UNIT : 0));
    app_timer_register(FAST_TIME_MS, fast_time_handler, NULL);
}
#endif // FAST_TIME_MS

static void new_config_handler(const Config* config) {
    handle_config(config);

    // The first activation must be done by init(), after the first call to tick_handler
    static bool is_init = true;
    if (is_init) {
        is_init = false;
    } else {
        activation_handler(NULL);
        mark_root_window_dirty(true);
    }
}

static void main_window_load(Window *window) {
    config_init(new_config_handler);

    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);

    connection_service_subscribe((ConnectionHandlers){handle_connection, NULL});
    accel_tap_service_subscribe(accel_tap_handler);

    new_config_handler(config_get());
}

static void main_window_unload(Window *window) {
    destroy_resources();
    accel_tap_service_unsubscribe();
    tick_timer_service_unsubscribe();
    config_deinit();
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

    activation_handler(NULL);

#if FAST_TIME_MS
    s_fake_time = time(NULL);
    app_timer_register(FAST_TIME_MS, fast_time_handler, NULL);
#endif // FAST_TIME_MS
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
