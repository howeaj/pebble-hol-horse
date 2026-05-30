// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    - Test all tick placements
    - 1 minute fast
    - Rounder minute hand bobble?
    - If seconds are visible at noon, play shoot animation panels
    - Za Waldo stop time and invert colours for 5 seconds... but when to trigger?
        - bluetooth disconnect?
        - on charger?
    - complications
        - bluetooth
        - steps
    - config:
        1 min fast
        Shake to activate
        Touch to activate
        Low-power-mode under 20%
        Colours
*/

#include <pebble.h>

#include "debug.h"
#include "dissolve.h"
#include "fliprot.h"
#include "macros.h"
#include "translate.h"

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

// Return the angle from a to b in TRIGANGLE units
static int32_t angle_between_points(GPoint a, GPoint b) {
    return atan2_lookup(b.x - a.x, b.y - a.y);  // TODO why are x and y reversed ???????
}

static bool should_show_seconds(void);
static void mark_root_window_dirty(void);


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
    // +1 round up to ensure the result is always > the precise angle
    if (units_changed & MINUTE_UNIT) {
        const int32_t hour_as_minutes = (((tick_time->tm_hour % 12) * MINUTES_PER_HOUR) + tick_time->tm_min);
        s_hand_angles.hour = MUL_FRACT(hour_as_minutes, TRIG_MAX_ANGLE, 12 * MINUTES_PER_HOUR) + 1;
        s_hand_angles.min = MUL_FRACT(tick_time->tm_min, TRIG_MAX_ANGLE, MINUTES_PER_HOUR) + 1;
    }
    s_hand_angles.sec = MUL_FRACT(tick_time->tm_sec, TRIG_MAX_ANGLE, SECONDS_PER_MINUTE) + 1;
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

static bool s_is_full_redraw = true;
static GFont s_font_letters;
static GFont s_font_numbers;

#define FRAMERATE (30)
#define MS_PER_FRAME (MS_PER_S / FRAMERATE)
#define BGCOLOR (GColorWhite)

#define NUM_COMPLICATIONS (3)
#define COMPLICATION_RADIUS MUL_FRACT(PBL_DISPLAY_WIDTH, 1, 5) // distance from centre to place each complication
#define FLOAT_PIXELS_PER_SECOND (6)
#define FLOAT_FRAMES_PER_PIXEL (FRAMERATE / FLOAT_PIXELS_PER_SECOND)

typedef enum ComplicationType {
    ComplicationType_Tick = 0,
    ComplicationType_Battery,
    ComplicationType_DayName,
    ComplicationType_DayNum,
    ComplicationType_MonthName,
    ComplicationType_MonthNum,
} ComplicationType;

typedef enum ComplicationState {
    ComplicationState_Off = 0,
    ComplicationState_Appear,
    ComplicationState_On,
    ComplicationState_Disappear,

    ComplicationState_MAX,  // end-of-enum sentinel
} ComplicationState;

typedef struct Complication {
    ComplicationType type;
    GBitmap* bitmap;
    char text[4];
    GFont font;
    GColor color;

    ComplicationState state;
    GRect rect;
    GPoint shake_offset;
    int32_t animation_counter;
    int32_t angle;
    int32_t arc;
} Complication;

static AppTimer* s_animation_timer = NULL;
static GBitmap* s_tick;
static Complication complications[NUM_COMPLICATIONS];

static const uint32_t FRAMES_PER_STATE[ComplicationState_MAX] = {
    0,              // ComplicationState_Off
    FRAMERATE / 4,  // ComplicationState_Appear
    MUL_FRACT(FRAMERATE, 11, 4),  // ComplicationState_On
    FRAMERATE / 2   // ComplicationState_Disappear
};

static inline int16_t complication_index(const Complication* complication) {
    return ((void*)complication - (void*)complications) / sizeof(Complication);
}

static inline bool complication_is_text(const Complication* complication) {
    return complication->type != ComplicationType_Tick;
}

/// Return how many pixels upwards the complication will move in its lifetime
static int16_t floatup_distance(const Complication* complication) {
    const int16_t num_frames = (int16_t)(
        FRAMES_PER_STATE[ComplicationState_On]
        + FRAMES_PER_STATE[ComplicationState_Disappear]
        - (complication_index(complication) * FRAMERATE)
    );
    return num_frames / FLOAT_FRAMES_PER_PIXEL;
}

/** Update `complication`'s rect and apply any special effects according to its ComplicationState.
    Return if the complication should be erased and/or drawn.
*/
static void apply_complication_state_effects(GContext* ctx, Complication* complication,
                                             bool state_changed, bool* erase, bool* draw) {
    switch (complication->state) {

        case ComplicationState_Off:
            *draw = false;
            break;

        case ComplicationState_Appear:
            complication->shake_offset = (GPoint) { // TODO try nonrandom shake
                (rand() % 3) - 1, (rand() % 3) - 1
            };
            complication->rect.origin.x += complication->shake_offset.x;
            complication->rect.origin.y += complication->shake_offset.y;
            break;

        case ComplicationState_On:
            if ((complication->animation_counter % FLOAT_FRAMES_PER_PIXEL) == 0) {
#if !DEBUG_SHOW_COMPL_RECTS
                complication->rect.origin.y -= 1;
#endif // !DEBUG_SHOW_COMPL_RECTS
            } else {
                *erase = state_changed;
                *draw = state_changed || s_is_full_redraw;
            }
            break;

        case ComplicationState_Disappear:
            // stop drawing the bitmap; "dissolve in" a background-coloured rect on top of it
            dissolve_rect(ctx, complication->rect, BGCOLOR);
            if ((complication->animation_counter % FLOAT_FRAMES_PER_PIXEL) == 0) {
#if !DEBUG_SHOW_COMPL_RECTS
                complication->rect.size.h += 1;
                translate_rect(ctx, complication->rect, GVector(0, -1));
                complication->rect.size.h -= 1;
                complication->rect.origin.y -= 1;
#endif // !DEBUG_SHOW_COMPL_RECTS
            }
            *erase = false;
            // The first frame of this state is a full redraw to remove the second hand.
            // This also means if we hit a minute during the dissolve, they'll all reappear
            // but that's impossible because it starts on a tick and only lasts 500ms.
            *draw = s_is_full_redraw;
            break;

        default:
            ASSERT(false);
            break;
    }
}

static void draw_complication(Layer* layer, GContext* ctx, Complication* complication) {
    if (complication->state == ComplicationState_Off) {
        return;
    }

    // Save the previous bounds for erasure, then undo any shake
    const GRect prev_rect = complication->rect;
    complication->rect.origin.x -= complication->shake_offset.x;
    complication->rect.origin.y -= complication->shake_offset.y;

    // Change state when counter expires
    bool state_changed = false;
    complication->animation_counter--;
    if (complication->animation_counter <= 0) {
        complication->state = (complication->state + 1) % ComplicationState_MAX;
        complication->animation_counter = FRAMES_PER_STATE[complication->state];
        complication->shake_offset = (GPoint) {0, 0};
        state_changed = true;
    }

    bool erase = !s_is_full_redraw;
    bool draw = true;
    apply_complication_state_effects(ctx, complication, state_changed, &erase, &draw);

    if (erase) {
        graphics_context_set_fill_color(ctx, BGCOLOR);
        graphics_fill_rect(ctx, prev_rect, 0, GCornerNone);
    }
    if (draw) {
#if DEBUG_SHOW_COMPL_RECTS
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_fill_rect(ctx, complication->rect, 0, GCornerNone);
#endif // DEBUG_SHOW_COMPL_RECTS
        if (complication_is_text(complication)) {
            graphics_context_set_text_color(ctx, complication->color);
            graphics_draw_text(ctx, complication->text, complication->font, complication->rect,
                GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
        } else {
            graphics_draw_bitmap_in_rect(ctx, complication->bitmap, complication->rect);
        }
    }
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

static void complications_nextframe(void* context) {
    mark_root_window_dirty();
    if (num_active_complications() > 0) {
        s_animation_timer = app_timer_register(MS_PER_FRAME, complications_nextframe, NULL);
    } else {
        s_animation_timer = NULL;
    }
}

static void stop_complications(void) {
    // assumes all complications are already on
    uint32_t delay = 0;
    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        complications[i].animation_counter = delay;
        delay += FRAMERATE / 10;
    }
    // the last complication is still in State_Appear
    complications[NUM_COMPLICATIONS - 1].state = ComplicationState_On;
    complications[NUM_COMPLICATIONS - 1].shake_offset = (GPoint) {0, 0};
}

static void update_complication_value(Complication* complication) {
    const time_t now = time(NULL);
    switch (complication->type) {
        case ComplicationType_Battery:
            const int len = snprintf(complication->text, 4, "%u%%", MIN(99, battery_state_service_peek().charge_percent));
            ASSERT((len == 1) || (len == 2));  // suppress GCC -Wformat-truncation
            break;
        case ComplicationType_DayName:
            strftime(complication->text, 4, "%a", localtime(&now));
            break;
        case ComplicationType_DayNum:
            strftime(complication->text, 4, "%d", localtime(&now));
            // TODO add small st/nd/rd/th second text draw
            break;
        case ComplicationType_MonthName:
            strftime(complication->text, 4, "%b", localtime(&now));
            break;
        case ComplicationType_MonthNum:
            strftime(complication->text, 4, "%m", localtime(&now));
            break;
        case ComplicationType_Tick:
            complication->bitmap = s_tick;
            break;
        default:
            ASSERT(false);
            break;
    }
}

static void update_complication_size(Complication* complication) {
    switch (complication->type) {
        case ComplicationType_Battery:
        case ComplicationType_DayNum:
        case ComplicationType_MonthNum:
            complication->font = s_font_numbers;
            complication->rect.size = (GSize){50, 25};
            break;
        case ComplicationType_DayName:
        case ComplicationType_MonthName:
            complication->font = s_font_letters;
            complication->rect.size = (GSize){53, 22};
            break;
        case ComplicationType_Tick:
            complication->rect = gbitmap_get_bounds(complication->bitmap);
            break;
        default:
            ASSERT(false);
            break;
    }
}

static void start_complication(Complication* complication) {

    update_complication_value(complication);
    update_complication_size(complication);

    ///////// Choose starting location
    // Calculate the total size of the complication including the distance it moves
    // note this doesn't count the +-1 pixel shake on appear
    const GPoint display_centre = {PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_WIDTH / 2};
    const int16_t index = complication_index(complication);
    const int16_t floatup_dist = floatup_distance(complication);
    const GSize total_size = {
        complication->rect.size.w, complication->rect.size.h + floatup_dist
    };

    // Start searching from the previous complication's angle
    // TODO starting from 0 can waste some space; start from the hour hand? start from a random hand?
    int32_t start_angle_deg = 0;
    if (index > 0) {
        const Complication* const other = &complications[index - 1];
        start_angle_deg = TRIGANGLE_TO_DEG(other->angle + MIN_TO_TRIGANGLE(4)) % 360;
    }

    // Search for an angle with enough space to fit the complication for its whole life (total_size)
    // TODO maybe searching "downwards" helps to pack floaters?
    for (int32_t angle_deg = start_angle_deg; angle_deg < (start_angle_deg + 360); angle_deg += 6) {
        const int32_t angle = DEG_TO_TRIGANGLE(angle_deg % 360);
        // angle is the centrepoint of total_size; calculate its origin
        const GPoint bitmap_centre = point_from_angle(display_centre, angle, COMPLICATION_RADIUS);
        const GPoint origin = {
            bitmap_centre.x - total_size.w / 2,
            bitmap_centre.y - total_size.h / 2
        };
        // find the arc angle covered by the rect
        const int32_t corner_angles[4] = {
            angle_between_points(display_centre, origin),
            angle_between_points(display_centre, (GPoint){origin.x, origin.y + total_size.h}),
            angle_between_points(display_centre, (GPoint){origin.x + total_size.w, origin.y}),
            angle_between_points(display_centre, (GPoint){origin.x + total_size.w, origin.y + total_size.h}),
        };
        // LOG("%d %d %d %d", TRIGANGLE_TO_DEG(comp_angles[0]), TRIGANGLE_TO_DEG(comp_angles[1]),
        //                    TRIGANGLE_TO_DEG(comp_angles[2]), TRIGANGLE_TO_DEG(comp_angles[3]));
        const int32_t corner_to_corner_arc_angle[6] = {
            ABSDIFF_WRAP(corner_angles[0], corner_angles[1], TRIG_MAX_ANGLE),
            ABSDIFF_WRAP(corner_angles[0], corner_angles[2], TRIG_MAX_ANGLE),
            ABSDIFF_WRAP(corner_angles[0], corner_angles[3], TRIG_MAX_ANGLE),
            ABSDIFF_WRAP(corner_angles[1], corner_angles[2], TRIG_MAX_ANGLE),
            ABSDIFF_WRAP(corner_angles[1], corner_angles[3], TRIG_MAX_ANGLE),
            ABSDIFF_WRAP(corner_angles[2], corner_angles[3], TRIG_MAX_ANGLE),
        };
        int32_t arc = 0;
        for (size_t i = 0; i < ARRAY_LEN(corner_to_corner_arc_angle); i++) {
            if (corner_to_corner_arc_angle[i] > arc) {
                arc = corner_to_corner_arc_angle[i];
            }
        }
        const int32_t arc_centre_angle = angle;  // TODO this is not true for rects

        // Check for collision with clockhands
        const HandAngles hand_angles = analogue_get_angles();
        const int32_t second_centre = hand_angles.sec + (
              (index == 0) ?  SEC_TO_TRIGANGLE(1)
            : (index == 2) ? -SEC_TO_TRIGANGLE(1)
            : 0
        );
        // TODO the hour centre is also different
        // TODO if we're close to a minute, the hour/min hands are going to move...
        if (
            (ABSDIFF_WRAP(arc_centre_angle, second_centre,    TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(2.5) + arc) / 2))
         || (ABSDIFF_WRAP(arc_centre_angle, hand_angles.min,  TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(3.5) + arc) / 2))
         || (ABSDIFF_WRAP(arc_centre_angle, hand_angles.hour, TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(5.5) + arc) / 2))
        ) {
            continue;
        }

        // Check for collision with other complications
        bool overlaps_other_complication = false;
        // TODO get closer by taking into account the floating movement
        for (int32_t i = 0; i < index; i++) {
            const Complication* const other = &complications[i];
            if (ABSDIFF_WRAP(arc_centre_angle, other->angle, TRIG_MAX_ANGLE) < ((other->arc + arc)/2 - DEG_TO_TRIGANGLE(15))) {
                overlaps_other_complication = true;
                break;
            }
        }
        if (overlaps_other_complication) {
            continue;
        }

        // Success!
        complication->angle = arc_centre_angle;
        complication->arc = arc;
        complication->rect.origin = origin;
        complication->rect.origin.y += floatup_dist;
        break;
    }

#if DEBUG_SHOW_COMPL_RECTS
    complication->rect.size.h += floatup_distance(complication);
    complication->rect.origin.y -= floatup_distance(complication);
#endif // DEBUG_SHOW_COMPL_RECTS

    complication->state = ComplicationState_Appear;
    complication->animation_counter = FRAMES_PER_STATE[complication->state];
    complication->shake_offset = (GPoint) {0, 0};
}

static void configure_complications(void) {
    complications[0].type = ComplicationType_DayName;
    complications[0].color = GColorPurple;
    complications[1].type = ComplicationType_DayNum;
    complications[1].color = GColorPurple;
    complications[2].type = ComplicationType_MonthName;
    complications[2].color = GColorPurple;
}


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
        angle + (DEGREES_PER_HOUR_HAND / 2),  // offset input angle to minimise final display error
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
    // TRACE("handle_tick; s_num_seconds_shown = %d", s_num_seconds_shown);
    analogue_update_angles(tick_time, units_changed);

    if (should_show_seconds()) {
        start_complication(&complications[s_num_seconds_shown]);
        s_num_seconds_shown = MIN(MAX_NUM_SECONDS, s_num_seconds_shown + 1);
        if (s_animation_timer == NULL) {
            complications_nextframe(NULL);
        }
    } else {
        if (s_num_seconds_shown > 0) {
            stop_complications();
            s_num_seconds_shown = 0;
        }
    }

    s_is_full_redraw = true;
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

    const GRect bounds = layer_get_bounds(layer);

    graphics_context_set_compositing_mode(ctx, GCompOpSet); // enable transparency
    if (s_is_full_redraw)
    {

#if DEBUG_DRAW_FACE
        graphics_context_set_fill_color(ctx, BGCOLOR);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
#endif // DEBUG_DRAW_FACE

        // baked face bitmap
        graphics_draw_bitmap_in_rect(ctx, s_face_real, bounds);

#if DEBUG_DRAW_FACE
        draw_face(bounds, ctx);
#endif // DEBUG_DRAW_FACE

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

    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        draw_complication(layer, ctx, &complications[i]);
    }

    s_is_full_redraw = false;

    PROFILE_END("draw_root_layer");
}

static void set_bitmap_colors(void) {
    gbitmap_set_color(s_tick, 0, GColorPurple);
}

static void init_resources(Layer *root_layer) {
    s_font_letters = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_COMIC_LETTERS_22));
    s_font_numbers = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_COMIC_NUMBERS_22));
    s_face_real = gbitmap_create_with_resource(RESOURCE_ID_FACE_REAL);
    s_tick = gbitmap_create_with_resource(RESOURCE_ID_TICK);
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

    set_bitmap_colors();

    configure_complications(); // TODO move to handle_config
}

static void destroy_resources(void) {
    fonts_unload_custom_font(s_font_letters);
    fonts_unload_custom_font(s_font_numbers);
    gbitmap_destroy(s_face_real);
    gbitmap_destroy(s_tick);
    for (size_t i = 0; i < ARRAY_LEN(s_hour_hand_bitmaps); i++) {
        gbitmap_destroy(s_hour_hand_bitmaps[i]);
    }
    for (size_t i = 0; i < ARRAY_LEN(s_min_hand_bitmaps); i++) {
        gbitmap_destroy(s_min_hand_bitmaps[i]);
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

static void mark_root_window_dirty(void) {
    layer_mark_dirty(window_get_root_layer(s_main_window));
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    // TRACE("tick_handler; s_active_ticks_remaining = %d", s_active_ticks_remaining);
    handle_tick(tick_time, units_changed);
    mark_root_window_dirty();

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
    }
    s_active_ticks_remaining = ACTIVE_DURATION_S;
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

static void main_window_load(Window *window) {
    Layer* const root_layer = window_get_root_layer(window);
    layer_set_update_proc(root_layer, draw_root_layer);
    init_resources(root_layer);
    accel_tap_service_subscribe(accel_tap_handler);
    app_timer_register(1, activation_handler, NULL);
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
