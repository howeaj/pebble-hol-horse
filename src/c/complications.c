// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#include "complications.h"

#include "analogue.h"
#include "debug.h"
#include "dissolve.h"
#include "macros.h"
#include "misc.h"
#include "translate.h"

#include <pebble.h>


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
    uint32_t value;
    GFont font;
    GColor color1;  // main
    GColor color2;  // shadow

    ComplicationState state;
    GRect rect;
    GPoint shake_offset;
    int32_t animation_counter;
    int32_t angle;
    int32_t arc;
} Complication;

static GFont s_font_letters;
static GFont s_font_numbers;
static GFont s_font_suffix;
static AppTimer* s_animation_timer = NULL;
static GBitmap* s_tick;
static Complication complications[NUM_COMPLICATIONS];

static const uint32_t s_frames_per_state[ComplicationState_MAX] = {
    0,              // ComplicationState_Off
    FRAMERATE / 4,  // ComplicationState_Appear
    MUL_FRACT(FRAMERATE, 11, 4),  // ComplicationState_On
    FRAMERATE / 2   // ComplicationState_Disappear
};

static inline bool complication_is_text(const Complication* complication) {
    return complication->type != ComplicationType_Tick;
}

/// Return how many pixels upwards the complication will move in its lifetime
static int16_t floatup_distance(uint32_t index) {
    const int16_t num_frames = (int16_t)(
        s_frames_per_state[ComplicationState_On]
        + s_frames_per_state[ComplicationState_Disappear]
        - (index * FRAMERATE)
    );
    return num_frames / FLOAT_FRAMES_PER_PIXEL;
}

// starting offset of the ordinal suffix to leave a 1px gap before it
const int8_t s_ordinal_suffix_offset[31] = {
    4,   // 1
    14,  // 2
    14,  // 3
    18,  // 4
    15,  // 5
    16,  // 6
    7,   // 7
    17,  // 8
    14,  // 9
    24,  // 10
    12,  // 11
    21,  // 12
    23,  // 13
    25,  // 14
    22,  // 15
    23,  // 16
    14,  // 17
    24,  // 18
    21,  // 19
    34,  // 20
    21,  // 21
    33,  // 22
    32,  // 23
    35,  // 24
    32,  // 25
    33,  // 26
    24,  // 27
    34,  // 28
    31,  // 29
    34,  // 30
    21,  // 31
};

static inline GRect suffix_rect(GRect rect, int32_t value) {
    ASSERT((size_t)value <= ARRAY_LEN(s_ordinal_suffix_offset));
    const int16_t dx = s_ordinal_suffix_offset[value - 1];
    const int16_t h = 10;
    return GRect(rect.origin.x + dx, rect.origin.y + (rect.size.h - h), rect.size.w - dx, h);
}

#define SHAD_OFF (2)// Pixel offset of the complications' shadow
static inline GRect shadow_rect(GRect rect) {
    return GRect(rect.origin.x - SHAD_OFF, rect.origin.y + SHAD_OFF, rect.size.w, rect.size.h);
}
static inline GRect rect_with_shadow(GRect rect) {
    return GRect(rect.origin.x - SHAD_OFF, rect.origin.y, rect.size.w + SHAD_OFF, rect.size.h + SHAD_OFF);
}

/** Update `complication`'s rect and apply any special effects according to its ComplicationState.
    Return if the complication should be erased and/or drawn.
*/
static void apply_complication_state_effects(GContext* ctx, Complication* complication, bool is_full_redraw,
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
                *draw = state_changed || is_full_redraw;
            }
            break;

        case ComplicationState_Disappear:
            // stop drawing the bitmap; "dissolve in" a background-coloured rect on top of it
            dissolve_rect(ctx, rect_with_shadow(complication->rect), BGCOLOR);
            if ((complication->animation_counter % FLOAT_FRAMES_PER_PIXEL) == 0) {
#if !DEBUG_SHOW_COMPL_RECTS
                complication->rect.size.h += 1;
                translate_rect(ctx, rect_with_shadow(complication->rect), GVector(0, -1));
                complication->rect.size.h -= 1;
                complication->rect.origin.y -= 1;
#endif // !DEBUG_SHOW_COMPL_RECTS
            }
            *erase = false;
            // The first frame of this state is a full redraw to remove the second hand.
            // This also means if we hit a minute during the dissolve, they'll all reappear
            // but that's impossible because it starts on a tick and only lasts 500ms.
            *draw = is_full_redraw;
            break;

        default:
            ASSERT(false);
            break;
    }
}

/// Return the ordinal suffix for a number.
/// Only correct for values <= 110
static const char* ordinal_suffix(uint32_t value) {
    if (WITHIN(value, 11, 13)) {
        value = 0;
    }
    switch (value % 10) {
        case 1:
            return "st";
            break;
        case 2:
            return "nd";
            break;
        case 3:
            return "rd";
            break;
        default:
            return "th";
            break;
    }
}

static void draw_text_with_shadow(GContext* ctx, const char* text, GFont font,
                                  GRect rect, GTextOverflowMode overflow_mode, GTextAlignment alignment,
                                  GColor color_text, GColor color_shadow) {
    graphics_context_set_text_color(ctx, color_shadow);
    graphics_draw_text(ctx, text, font, shadow_rect(rect), GTextOverflowModeWordWrap, alignment, NULL);
    graphics_context_set_text_color(ctx, color_text);
    graphics_draw_text(ctx, text, font, rect, GTextOverflowModeWordWrap, alignment, NULL);
}

// Perform the graphics draw operation to draw `complication` in `rect` with the given `color`s
static void graphics_draw_complication(GContext* ctx, Complication* complication,
                                       GRect rect, GColor color1, GColor color2) {
    if (complication_is_text(complication)) {
        const GTextAlignment alignment =
            (complication->type == ComplicationType_DayNum) ? GTextAlignmentLeft : GTextAlignmentCenter;
        if ((alignment == GTextAlignmentLeft) && (complication->text[1] == '\0')) {  // manually ~centre text
            rect.origin.x += 20;
            rect.size.w -= 20;
        }

        draw_text_with_shadow(ctx, complication->text, complication->font, rect,
            GTextOverflowModeWordWrap, alignment, color1, color2);

        // ordinal suffix
        if (complication->type == ComplicationType_DayNum) {
            draw_text_with_shadow(ctx, ordinal_suffix(complication->value), s_font_suffix,
                suffix_rect(rect, complication->value),
                GTextOverflowModeWordWrap, GTextAlignmentLeft, color1, color2);
        }
    } else {
        gbitmap_set_color(complication->bitmap, 0, color2);
        graphics_draw_bitmap_in_rect(ctx, complication->bitmap, shadow_rect(rect));
        gbitmap_set_color(complication->bitmap, 0, color1);
        graphics_draw_bitmap_in_rect(ctx, complication->bitmap, rect);
    }
}

void complication_draw(Layer* layer, GContext* ctx, uint32_t index, bool is_full_redraw) {
    UNUSED(layer);
    Complication* const complication = &complications[index];
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
        complication->animation_counter = s_frames_per_state[complication->state];
        complication->shake_offset = (GPoint) {0, 0};
        state_changed = true;
    }

    bool erase = !is_full_redraw;
    bool draw = true;
    apply_complication_state_effects(ctx, complication, is_full_redraw, state_changed, &erase, &draw);

    if (erase) {
        // If our placements were perfect it would be fine to erase the whole rect, but they aren't...
        // graphics_context_set_fill_color(ctx, BGCOLOR);
        // graphics_fill_rect(ctx, prev_rect, 0, GCornerNone);

        // ...so instead only erase exactly what we drew
        graphics_draw_complication(ctx, complication, prev_rect, BGCOLOR, BGCOLOR);
    }

    if (draw) {
#if DEBUG_SHOW_COMPL_RECTS
        graphics_context_set_fill_color(ctx, GColorRed);
        graphics_fill_rect(ctx, complication->rect, 0, GCornerNone);
#endif // DEBUG_SHOW_COMPL_RECTS
        graphics_draw_complication(ctx, complication, complication->rect, complication->color1, complication->color2);
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

static void update_complication_value(Complication* complication) {
#if DEBUG_COMPL_VAL
    static time_t now = 0;
    now += SECONDS_PER_DAY * (DEBUG_COMPL_VAL - 1);
#else // !DEBUG_COMPL_VAL
    const time_t now = time(NULL);
#endif // !DEBUG_COMPL_VAL

    switch (complication->type) {
        case ComplicationType_Battery:
            complication->value = battery_state_service_peek().charge_percent;
            const int len = snprintf(complication->text, 4, "%u%%", (uint8_t)MIN(99, complication->value));
            ASSERT((len == 1) || (len == 2));  // suppress GCC -Wformat-truncation
            break;
        case ComplicationType_DayName:
            strftime(complication->text, 4, "%a", localtime(&now));
            break;
        case ComplicationType_DayNum:
            const struct tm* now_tm = localtime(&now);
            complication->value = now_tm->tm_mday;  // save value for suffix_rect()
            // use snprintf to avoid leading 0
            snprintf(complication->text, 4, "%u", (uint8_t)complication->value);
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

void complication_start(uint32_t index) {
    ASSERT(index < NUM_COMPLICATIONS);
    Complication* const complication = &complications[index];

    update_complication_value(complication);
    update_complication_size(complication);

    ///////// Choose starting location
    // Calculate the total size of the complication including the distance it moves
    // note this doesn't count the +-1 pixel shake on appear
    const GPoint display_centre = {PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_WIDTH / 2};
    const int16_t floatup_dist = floatup_distance(index);
    const GRect full_rect = rect_with_shadow(complication->rect);
    const GSize total_size = {
        full_rect.size.w, full_rect.size.h + floatup_dist
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
        for (uint32_t i = 0; i < index; i++) {
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

        #if DEBUG_SHOW_COMPL_RECTS
            complication->rect.size.h += floatup_distance(index);
            complication->rect.origin.y -= floatup_distance(index);
        #endif // DEBUG_SHOW_COMPL_RECTS

        complication->state = ComplicationState_Appear;
        complication->animation_counter = s_frames_per_state[complication->state];
        complication->shake_offset = (GPoint) {0, 0};

        if (s_animation_timer == NULL) {
            complications_nextframe(NULL);
        }
        return;
    }

    LOG_ERR("Failed to place complication");
}

void complications_stop(void) {
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

static void configure_complications(void) {
    complications[0].type = ComplicationType_DayNum;
    complications[0].color1 = GColorPurple;
    complications[0].color2 = GColorGreen;
    complications[1].type = ComplicationType_MonthName;
    complications[1].color1 = GColorPurple;
    complications[1].color2 = GColorGreen;
    complications[2].type = ComplicationType_Tick;
    complications[2].color1 = GColorPurple;
    complications[2].color2 = GColorGreen;
}

void complications_init(void) {
    s_font_letters = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_COMIC_LETTERS_22));
    s_font_numbers = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_COMIC_NUMBERS_22));
    s_font_suffix = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_COMIC_SUFFIX_10));
    s_tick = gbitmap_create_with_resource(RESOURCE_ID_TICK);
    configure_complications();
}

void complications_destroy(void) {
    fonts_unload_custom_font(s_font_letters);
    fonts_unload_custom_font(s_font_numbers);
    fonts_unload_custom_font(s_font_suffix);
    gbitmap_destroy(s_tick);
}
