// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    - Tick SFX
    - second leaves behind shadow?
    - 1 minute fast
    - If seconds are visible at noon, play shoot animation panels
    - Za Waldo stop time and invert colours for 5 seconds... but when to trigger?
        - bluetooth disconnect?
        - on charger?
    - complications (replace tick SFX)
        - bluetooth
        - battery
        - date
    - config:
        1 min fast
        Shake to activate
        Touch to activate
        Vibe on BT disconnect
        Vibe on hour
        Low-power-mode under 20%
        Invert colours
*/

#include <pebble.h>

#include "debug.h"
#include "fliprot.h"
#include "macros.h"

#if DEBUG
// Return milliseconds since the epoch
uint32_t timestamp_ms(void) {
    time_t seconds = 0;
    const uint32_t millis = (uint32_t)time_ms(&seconds, NULL);
    return ((uint32_t)seconds * MS_PER_S) + millis;
}
#endif // DEBUG

static inline GPoint gpoint_sub(const GPoint a, const GPoint b) {
    return GPoint(a.x - b.x, a.y - b.y);
}

// set the value at `index` in `bitmap`'s palette to `color`
static inline void gbitmap_set_color(GBitmap* bitmap, size_t index, GColor color) {
    gbitmap_get_palette(bitmap)[index] = color;
}

// Return a GPoint that is `distance` away from `origin` at `angle`.
// If `origin` is 0, this is equivalent to converting `angle` to a cartesian vector of magnitude `distance`.
static GPoint point_from_angle(GPoint origin, int32_t angle, int32_t distance) {
    return (GPoint) {
        .x = (int16_t)((sin_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.x,
        .y = (int16_t)((-cos_lookup(angle) * distance) / TRIG_MAX_RATIO) + origin.y
    };
}

static bool should_show_seconds(void);


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
 This watchface implementation; complications
******************************************************************************/


#define NUM_COMPLICATIONS (3)
#define COMPLICATION_RADIUS MUL_FRACT(PBL_DISPLAY_WIDTH, 1, 5) // distance from centre to place each complication
#define COMPLICATION_ARC_ANGLE DEG_TO_TRIGANGLE(50)  // how many degrees does it cover (depends on COMPLICATION_RADIUS)
#define HAND_ARC_ANGLE DEG_TO_TRIGANGLE((360 / MINUTES_PER_HOUR) * 3)  // how many degrees does each hand cover

typedef enum ComplicationType {
    ComplicationType_Tick = 0,
} ComplicationType;

typedef enum ComplicationState {
    ComplicationState_Off = 0,
    ComplicationState_Appear,
    ComplicationState_On,
    ComplicationState_Disappear,
} ComplicationState;

typedef struct Complication {
    ComplicationType type;
    GBitmap* bitmap;
    GRect rect;

    ComplicationState state;
    int32_t animation_counter;
} Complication;

static GBitmap* s_tick;
static Complication complications[NUM_COMPLICATIONS];

static void draw_complication(Layer* layer, GContext* ctx, const Complication* complication) {
    switch(complication->state) {
    case ComplicationState_Off:
        return;  // nothing to draw
        break;
    case ComplicationState_Appear:
        break;
    case ComplicationState_On:
        break;
    case ComplicationState_Disappear:
        break;
    default:
        ASSERT(false);
        break;
    }

    graphics_draw_bitmap_in_rect(ctx, complication->bitmap, complication->rect);
}

static int8_t num_active_complications(void) {
    int8_t count = 0;
    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        if (complications[i].state != ComplicationState_Off) {
            count++;
        }
    }
    return count;
}

static void start_complication(Complication* complication) {
    switch (complication->type) {
    case ComplicationType_Tick:
    default:
        complication->bitmap = s_tick;
        break;
    }

    // choose location
    const HandAngles hand_angles = analogue_get_angles();
    int32_t angle = DEG_TO_TRIGANGLE(45);
    int8_t remaining_complications = num_active_complications() * 2;
    do {
        angle += (COMPLICATION_ARC_ANGLE / 2);
    } while (
           (ABSDIFF_WRAP(angle % TRIG_MAX_ANGLE, hand_angles.sec, TRIG_MAX_ANGLE) < ((HAND_ARC_ANGLE + COMPLICATION_ARC_ANGLE) / 2))
        || (ABSDIFF_WRAP(angle % TRIG_MAX_ANGLE, hand_angles.min, TRIG_MAX_ANGLE) < ((HAND_ARC_ANGLE + COMPLICATION_ARC_ANGLE) / 2))
        || (ABSDIFF_WRAP(angle % TRIG_MAX_ANGLE, hand_angles.hour, TRIG_MAX_ANGLE) < ((HAND_ARC_ANGLE + COMPLICATION_ARC_ANGLE) / 2))
        || (remaining_complications-- > 0)
    );
    angle = angle % TRIG_MAX_ANGLE;
    const GPoint display_centre = {PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_WIDTH / 2};
    const GPoint bitmap_centre = point_from_angle(display_centre, angle, COMPLICATION_RADIUS);
    complication->rect = gbitmap_get_bounds(complication->bitmap);
    complication->rect.origin = (GPoint) {
        bitmap_centre.x - complication->rect.size.w / 2,
        bitmap_centre.y - complication->rect.size.h / 2
    };

    complication->state = ComplicationState_Appear;
    complication->animation_counter = 0;
}


/******************************************************************************
 This watchface implementation; hands
******************************************************************************/

#define NUM_HAND_BITMAPS (7)  // The number of unique hand bitmaps
#define HAND_BITMAPS_RANGE_DEG (45)  // The range of degrees covered by our hand bitmaps

static GBitmap* s_hour_hand_bitmaps[NUM_HAND_BITMAPS];


static void draw_hand_min(Layer* layer, GContext* ctx, int32_t angle) {
    // const GRect bounds = layer_get_bounds(layer);
    // const GPoint centre = grect_center_point(&bounds);
    // const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) - MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 2, 5));
    // TODO
}

static void draw_hand_hour(Layer* layer, GContext* ctx, int32_t angle) {
    FliprotFlags fliprot_flags;
    const size_t bitmap_index = fliprot_hand_bitmap_index(
        NUM_HAND_BITMAPS, HAND_BITMAPS_RANGE_DEG, angle, &fliprot_flags);

    // Select the target draw origin so that its centrepoint is in the centre of the screen.
    static const GPoint bitmap_centre_points[NUM_HAND_BITMAPS] = {
        {8, 79},  // 0
        {4, 77},  // 7.5
        {2, 74},  // 15
        {1, 71},  // 22.5
        {0, 67},  // 30
        {0, 61},  // 37.5
        {0, 54},  // 45
    };
    const GRect layer_bounds = layer_get_bounds(layer);
    const GPoint layer_centre = grect_center_point(&layer_bounds);
    const GPoint bitmap_centre = bitmap_centre_points[bitmap_index];
    const GBitmap* bitmap = s_hour_hand_bitmaps[bitmap_index];
    GRect target_rect = gbitmap_get_bounds(bitmap);
    target_rect.origin = gpoint_sub(layer_centre, bitmap_centre);

    fliprot_draw_bitmap(ctx, bitmap, target_rect, fliprot_flags);
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

static void draw_hand_sec_shadow(Layer* layer, GContext* ctx, int32_t angle) {
    const GRect bounds = layer_get_bounds(layer);
    const GPoint centre = grect_center_point(&bounds);
    const GRect mid_bounds = grect_crop(bounds, (PBL_DISPLAY_WIDTH / 6) + MUL_FRACT(PBL_DISPLAY_WIDTH / 6, 1, 5));
    const GPoint tip = gpoint_from_polar(mid_bounds, GOvalScaleModeFitCircle, angle);

    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, centre, tip);
}


/******************************************************************************
 This watchface implementation; handlers
******************************************************************************/

static GBitmap* s_face_real;

#define MAX_NUM_SECONDS (3)
static int8_t s_num_seconds_shown = 0;


static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
    TRACE("handle_tick; s_num_seconds_shown = %d", s_num_seconds_shown);
    analogue_update_angles(tick_time, units_changed);

    if (should_show_seconds()) {
        start_complication(&complications[s_num_seconds_shown]);
        s_num_seconds_shown = MIN(MAX_NUM_SECONDS, s_num_seconds_shown + 1);
    } else {
        s_num_seconds_shown = 0;
    }
}

#define NUM_CLOCK_INDICES (60)
static void draw_root_layer(Layer* layer, GContext* ctx) {
    TRACE("draw_root_layer; s_num_seconds_shown = %d", s_num_seconds_shown);
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

    for (int8_t i = 1; i < s_num_seconds_shown; i++) {
        draw_hand_sec_shadow(layer, ctx, hand_angles.sec - (i * (TRIG_MAX_ANGLE / MINUTES_PER_HOUR)));
    }
    if (s_num_seconds_shown > 0) {
        draw_hand_sec(layer, ctx, hand_angles.sec);
    }

    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        draw_complication(layer, ctx, &complications[i]);
    }

    // static int i = 0;
    // draw_hand_hour(layer, ctx, DEG_TO_TRIGANGLE(i * (360 / 60)));
    // i = i + 1 % 60;

    PROFILE_END("draw_root_layer");
}

static void set_bitmap_colors(void) {
    gbitmap_set_color(s_tick, 0, GColorPurple);
}

static void init_resources(Layer *root_layer) {
    s_face_real = gbitmap_create_with_resource(RESOURCE_ID_FACE_REAL);
    s_tick = gbitmap_create_with_resource(RESOURCE_ID_TICK);
    s_hour_hand_bitmaps[0] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_0);
    s_hour_hand_bitmaps[1] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_7_5);
    s_hour_hand_bitmaps[2] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_15);
    s_hour_hand_bitmaps[3] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_22_5);
    s_hour_hand_bitmaps[4] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_30);
    s_hour_hand_bitmaps[5] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_37_5);
    s_hour_hand_bitmaps[6] = gbitmap_create_with_resource(RESOURCE_ID_HAND_HOUR_45);

    set_bitmap_colors();
}

static void destroy_resources(void) {
    gbitmap_destroy(s_face_real);
    gbitmap_destroy(s_tick);
    for (size_t i = 0; i < ARRAY_LEN(s_hour_hand_bitmaps); i++) {
        gbitmap_destroy(s_hour_hand_bitmaps[i]);
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

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    TRACE("tick_handler; s_active_ticks_remaining = %d", s_active_ticks_remaining);
    handle_tick(tick_time, units_changed);
    layer_mark_dirty(window_get_root_layer(s_main_window));

    if (s_active_ticks_remaining == 0) {
        LOG("Schedule minute updates");
        tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    }
    if (s_active_ticks_remaining >= 0) {
        s_active_ticks_remaining -= 1;
    }
}

static void activation_handler(void* context) {
    UNUSED(context);
    if (s_active_ticks_remaining == -1) {
        LOG("Schedule second updates");
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    }
    s_active_ticks_remaining = ACTIVE_DURATION_S;
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
    activation_handler(NULL);
}

static void main_window_load(Window *window) {
    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);
    accel_tap_service_subscribe(accel_tap_handler);
    activation_handler(NULL);
}

static void main_window_unload(Window *window) {
    destroy_resources();
    accel_tap_service_unsubscribe();
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
