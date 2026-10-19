// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

/* TODO
    Double redraw on minute tick
*/

#include "complications.h"

#include "analogue.h"
#include "bitblt.h"
#include "config.h"
#include "debug.h"
#include "dissolve.h"
#include "macros.h"
#include "misc.h"
#include "translate.h"

#include <pebble.h>


#define FRAMERATE (30)
#define MS_PER_FRAME (MS_PER_S / FRAMERATE)

#define NUM_COMPLICATIONS (3)
#define COMPLICATION_RADIUS MUL_FRACT(PBL_DISPLAY_WIDTH, 1, 5) // distance from centre to place each complication
#define FLOAT_PIXELS_PER_SECOND (6)
#define FLOAT_FRAMES_PER_PIXEL (FRAMERATE / FLOAT_PIXELS_PER_SECOND)
#define MAX_SHAKE (1)  // Maximum pixel offset from centre for Complication->shake_offset
#define SHAD_OFF (2)  // Pixel offset of the complications' shadow

typedef enum ComplicationState {
    ComplicationState_Off = 0,
    ComplicationState_Appear,
    ComplicationState_On,
    ComplicationState_Disappear,

    ComplicationState_MAX,  // end-of-enum sentinel
} ComplicationState;

#define MAX_TEXT_SIZE (6)  // Maximum characters (incl. null terminator) in text fields

typedef struct Complication {
    uint32_t index;  // The index of this complication in `complications`
    ComplicationMode mode;
    ComplicationType type;
    GBitmap* bitmap;
    GBitmap* bgbuffer;  // the content of the framebuffer beneath the complication's future bounds
    GPoint bgbuffer_origin;  // origin (in framebuffer frame of reference) of bgbuffer
    char text[MAX_TEXT_SIZE];
    GColor color1;  // main
    GColor color2;  // shadow

    /* `value` has a different meaning for each ComplicationType:
        ComplicationType_Tick:      N/A (bitmap only)
        ComplicationType_Menacing:  N/A (bitmap only)
        ComplicationType_Battery:   Remaining % (0-99), set by update_complication_display_value()
        ComplicationType_DayName:   N/A (text only)
        ComplicationType_DayNum:    N/A (text only)
        ComplicationType_MonthName: N/A (text only)
        ComplicationType_MonthNum:  N/A (text only)
        ComplicationType_Steps:     isDistance (0 or 1), set by complications_configure()
        ComplicationType_Sleep:     isRestful (0 or 1), set by complications_configure()
        ComplicationType_Timezone:  UTC offset (-12 to +14), set by complications_configure()
    */
    int32_t value;

    ComplicationState state;
    GRect rect;
    GPoint shake_offset;
    int32_t animation_counter;
    int32_t angle;
    int32_t arc;
} Complication;

static GFont s_font;
static AppTimer* s_animation_timer = NULL;
static GBitmap* s_tick;
static GBitmap* s_go;
static Complication s_complications[NUM_COMPLICATIONS];

static const uint32_t s_frames_per_state[ComplicationState_MAX] = {
    0,              // ComplicationState_Off
    FRAMERATE / 4,  // ComplicationState_Appear
    MUL_FRACT(FRAMERATE, 11, 4),  // ComplicationState_On
    FRAMERATE / 2   // ComplicationState_Disappear
};

static inline bool complication_is_text(const Complication* complication) {
    return complication->type > ComplicationType_Menacing;
}

/// Return how many pixels upwards the complication will move in its lifetime
/// TODO not really based on index, now that 2nd/3rd index can be the first to appear and/or not floating at all
static int16_t floatup_distance(uint32_t index) {
    const int16_t num_frames = (int16_t)(
        s_frames_per_state[ComplicationState_On]
        + s_frames_per_state[ComplicationState_Disappear]
        - (index * FRAMERATE)
    );
    return num_frames / FLOAT_FRAMES_PER_PIXEL;
}

// Return a random value (-MAXVAL <= x <= MAXVAL)
#define RAND_AROUND_0(MAXVAL) ((rand() % (1 + ((MAXVAL) * 2))) - (MAXVAL))

// The bounds of the rect's shadow
static inline GRect shadow_rect(GRect rect) {
    return GRect(rect.origin.x - SHAD_OFF, rect.origin.y + SHAD_OFF, rect.size.w, rect.size.h);
}

// The combined bounds of both the shadow and the original rect
static inline GRect rect_with_shadow(GRect rect) {
    return GRect(rect.origin.x - SHAD_OFF, rect.origin.y, rect.size.w + SHAD_OFF, rect.size.h + SHAD_OFF);
}

// Return the entire rect that a complication's bounds can cover for its entire life (incl. shadow, shake and float)
static inline GRect lifetime_rect(GRect rect, uint32_t index) {
    const int16_t max_float = floatup_distance(index);
    rect = rect_with_shadow(rect);
    return GRect(
        rect.origin.x - MAX_SHAKE,
        rect.origin.y - max_float,
        rect.size.w + (MAX_SHAKE * 2),
        rect.size.h + MAX_SHAKE + max_float
    );
}

// Return a rect equivalent to `child` with its origin moved to be relative to another `origin``
static inline GRect rect_relative_origin(GRect child, GPoint origin) {
    return (GRect){gpoint_sub(child.origin, origin), child.size};
}

// Return a rect that covers the bottom `rows` of `rect`
static inline GRect rect_bottom_rows(GRect rect, uint32_t rows) {
    return GRect(
        rect.origin.x,
        rect.origin.y + rect.size.h - rows,
        rect.size.w,
        rows
    );
}

// Save the current content of the framebuffer (prior to drawing the complication)
// into a complication's bgbuffer for future use by `complication_erase_rect`
static void complication_snapshot_rect(GContext* ctx, Complication* complication) {
    // Since we refresh this every second, we only need movement within 1 second
    // which is less than the lifetime movement of the final complication
    // TODO refactor lifetime_rect to like "rect_over_time" or something
    const GRect life_rect = lifetime_rect(complication->rect, NUM_COMPLICATIONS - 1);
    complication->bgbuffer_origin = life_rect.origin;
    #if DEBUG_SHOW_BGBUFFERS
        graphics_draw_rect(ctx, grect_crop(life_rect, -1));
    #endif // DEBUG_SHOW_BGBUFFERS

    GBitmap * const framebuffer = graphics_capture_frame_buffer(ctx);
    bitblt_bitmap_into_bitmap_tiled_8bit_to_8bit(
        complication->bgbuffer,  // dest
        framebuffer, // src
        gbitmap_get_bounds(complication->bgbuffer), // dest_rect
        complication->bgbuffer_origin  // src_origin_offset
    );
    graphics_release_frame_buffer(ctx, framebuffer);
}

// Erase any complication pixels from `rect` by copying pixels from its bgbuffer
static void complication_erase_rect(GContext* ctx, Complication* complication, GRect rect) {
    const GRect original_bounds = gbitmap_get_bounds(complication->bgbuffer);
    gbitmap_set_bounds(complication->bgbuffer, rect_relative_origin(rect, complication->bgbuffer_origin));
    graphics_draw_bitmap_in_rect(ctx, complication->bgbuffer, rect);
    gbitmap_set_bounds(complication->bgbuffer, original_bounds);
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
            // erase and redraw at random offsets for a "shake" effect
            complication->shake_offset = (GPoint) {
                RAND_AROUND_0(MAX_SHAKE), RAND_AROUND_0(MAX_SHAKE)
            };
            complication->rect.origin.x += complication->shake_offset.x;
            complication->rect.origin.y += complication->shake_offset.y;
            ASSERT(draw && (erase || is_full_redraw));
            break;

        case ComplicationState_On:
            // float upwards by erase & redraw
            if ((complication->animation_counter % FLOAT_FRAMES_PER_PIXEL) == 0) {
                #if !DEBUG_SHOW_COMPL_RECTS
                    complication->rect.origin.y -= 1;
                #endif // !DEBUG_SHOW_COMPL_RECTS
                ASSERT(draw && (erase || is_full_redraw));
            } else {
                *erase = state_changed;
                *draw = state_changed || is_full_redraw;
            }
            break;

        case ComplicationState_Disappear:
            // stop drawing the complication; "dissolve in" the background on top of it
            const GRect target_rect = rect_with_shadow(complication->rect);
            dissolve_bitmap_into_rect(
                ctx,
                target_rect,
                complication->bgbuffer,
                rect_relative_origin(target_rect, complication->bgbuffer_origin).origin
            );

            // also float upwards by framebuffer translation
            if ((complication->animation_counter % FLOAT_FRAMES_PER_PIXEL) == 0) {
                #if !DEBUG_SHOW_COMPL_RECTS
                    const GColor colorMask[2] = {complication->color1, complication->color2};
                    translate_rect(ctx, target_rect, GVector(0, -1), colorMask, 2);
                    const GRect bottom_row = rect_bottom_rows(target_rect, 1);
                    complication_erase_rect(ctx, complication, bottom_row);
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

/// Return the ordinal indicator for a number.
/// Only implemented correctly for values <= 110
static const char* ordinal_indicator(uint32_t value) {
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

static void draw_text_with_shadow(GContext* ctx, const char* text, GRect rect,
                                  GTextOverflowMode overflow_mode, GTextAlignment alignment,
                                  GColor color_text, GColor color_shadow) {
    graphics_context_set_text_color(ctx, color_shadow);
    graphics_draw_text(ctx, text, s_font, shadow_rect(rect), GTextOverflowModeWordWrap, alignment, NULL);
    graphics_context_set_text_color(ctx, color_text);
    graphics_draw_text(ctx, text, s_font, rect, GTextOverflowModeWordWrap, alignment, NULL);
}

// Draw a monochrome bitmap with a drop shadow. This modifies the bitmap's palette.
static inline void draw_bitmap_with_shadow(GContext* ctx, GBitmap* bitmap, GRect rect,
                                           GColor color_fg, GColor color_shadow) {
    gbitmap_set_color(bitmap, 0, color_shadow);
    graphics_draw_bitmap_in_rect(ctx, bitmap, shadow_rect(rect));
    gbitmap_set_color(bitmap, 0, color_fg);
    graphics_draw_bitmap_in_rect(ctx, bitmap, rect);
}

// Perform the graphics draw operation to draw `complication` in `rect` with the given `color`s
static void graphics_draw_complication(GContext* ctx, const Complication* complication,
                                       GRect rect, GColor color1, GColor color2) {
    if (complication_is_text(complication)) {
        draw_text_with_shadow(ctx, complication->text, rect,
            GTextOverflowModeWordWrap, GTextAlignmentCenter, color1, color2);
    } else {
        draw_bitmap_with_shadow(ctx, complication->bitmap, rect, color1, color2);
    }
}

static void draw_complication(GContext* ctx, const Complication* complication) {
#if DEBUG_SHOW_COMPL_RECTS
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_rect(ctx, complication->rect, 0, GCornerNone);
#endif // DEBUG_SHOW_COMPL_RECTS
    graphics_draw_complication(ctx, complication, complication->rect, complication->color1, complication->color2);
}

static void complication_draw_animated(GContext* ctx, Complication* complication, bool is_full_redraw) {
    // Save the previous bounds for erasure, then undo any shake
    const GRect prev_rect = rect_with_shadow(complication->rect);
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
        complication_erase_rect(ctx, complication, prev_rect);
    }

    if (draw) {
        draw_complication(ctx, complication);
    }
}

// Draw all static ("Always show") complications
static void complications_draw_static(GContext* ctx) {
    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        const Complication* const complication = &s_complications[i];
        if (complication->mode == ComplicationMode_OnAlways) {
            draw_complication(ctx, complication);
        }
    }
}

#define FOREACH_ACTIVATED_COMPLICATION \
    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) { \
        Complication* const complication = &s_complications[i]; \
        if ((complication->mode == ComplicationMode_OnActivate) \
            && (complication->state != ComplicationState_Off))
#define END_FOREACH_ACTIVATED_COMPLICATION }

// Snapshot the bgbuffer for all active "on activity" complications
static void complications_snapshot_activated(GContext* ctx) {
    FOREACH_ACTIVATED_COMPLICATION {
        complication_snapshot_rect(ctx, complication);
    } END_FOREACH_ACTIVATED_COMPLICATION
}

// Draw all active "on activity" complications
static void complications_draw_activated(GContext* ctx, bool is_full_redraw) {
    FOREACH_ACTIVATED_COMPLICATION {
        if ((config_get()->animationSpeed == AnimationSpeed_Off) && is_full_redraw) {  // TODO hmmmm
            draw_complication(ctx, complication);
        } else {
            complication_draw_animated(ctx, complication, is_full_redraw);
        }
    } END_FOREACH_ACTIVATED_COMPLICATION
}

void complications_draw(Layer* layer, GContext* ctx, bool is_full_redraw) {
    UNUSED(layer);
    if (is_full_redraw){
        complications_draw_static(ctx);
        complications_snapshot_activated(ctx);
    }
    complications_draw_activated(ctx, is_full_redraw);
}

// How many complications are currently animating
static int32_t num_animating_complications(void) {
    int32_t count = 0;
    if (config_get()->animationSpeed != AnimationSpeed_Off) {
        for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
            if ((s_complications[i].state != ComplicationState_Off)
                && (s_complications[i].mode == ComplicationMode_OnActivate))
            {
                count++;
            }
        }
    }
    return count;
}

static void complications_nextframe(void* context) {
    if (num_animating_complications() > 0) {
        mark_root_window_dirty(false);
        s_animation_timer = app_timer_register(MS_PER_FRAME, complications_nextframe, NULL);
    } else {
        LOG("LAST COMPLICATIONS_NEXTFRAME");
        s_animation_timer = NULL;
        complications_update();
        mark_root_window_dirty(true);
    }
}

static bool is_health_metric_accessible(HealthMetric metric) {
    const time_t start = time_start_of_today();
    const time_t end = time(NULL);
    const HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
    return mask & HealthServiceAccessibilityMaskAvailable;
}

// Return the value for the steps complication, in either steps or metres
static uint32_t get_steps_value(bool is_distance) {
    uint32_t val = 0;
    if (is_distance) {
        if (is_health_metric_accessible(HealthMetricWalkedDistanceMeters)) {
            val = (uint32_t)health_service_sum_today(HealthMetricWalkedDistanceMeters);
        }
        // TODO optionally convert to miles
    } else {
        if (is_health_metric_accessible(HealthMetricStepCount)) {
            val = (uint32_t)health_service_sum_today(HealthMetricStepCount);
        }
    }
    return val;
}

// Return the value for the sleep complication, either normal or restful
static uint32_t get_sleep_seconds(bool is_restful) {
    uint32_t val = 0;
    const HealthMetric metric = is_restful ? HealthMetricSleepRestfulSeconds : HealthMetricSleepSeconds;
    if (is_health_metric_accessible(metric)) {
        val = (uint32_t)health_service_sum_today(metric);
    }
    return val;
}

// Update `complication`'s display value, which could be complication->text, value and/or bitmap
static void update_complication_display_value(Complication* complication) {
#if DEBUG_COMPL_VAL
    static time_t now = 0;
    now += SECONDS_PER_DAY * (DEBUG_COMPL_VAL - 1);
#else // !DEBUG_COMPL_VAL
    const time_t now = time(NULL);
#endif // !DEBUG_COMPL_VAL

    switch (complication->type) {
        case ComplicationType_Tick: {
            complication->bitmap = s_tick;
            break;
        }
        case ComplicationType_Menacing: {
            complication->bitmap = s_go;
            break;
        }
        case ComplicationType_Battery: {
            complication->value = battery_state_service_peek().charge_percent;
            const int len = snprintf(complication->text, MAX_TEXT_SIZE, "%u%%", (uint8_t)MIN(99, complication->value));
            ASSERT((len == 2) || (len == 3));  // suppress GCC -Wformat-truncation
            break;
        }
        case ComplicationType_DayName: {
            strftime(complication->text, MAX_TEXT_SIZE, "%a", localtime(&now));
            string_to_upper(complication->text, MAX_TEXT_SIZE);
            break;
        }
        case ComplicationType_DayNum: {
            const struct tm* now_tm = localtime(&now);
            // use snprintf to avoid leading 0
            snprintf(complication->text, MAX_TEXT_SIZE, "%u%s",
                     now_tm->tm_mday, ordinal_indicator(now_tm->tm_mday));
            break;
        }
        case ComplicationType_MonthName: {
            strftime(complication->text, MAX_TEXT_SIZE, "%b", localtime(&now));
            string_to_upper(complication->text, MAX_TEXT_SIZE);
            break;
        }
        case ComplicationType_MonthNum: {
            strftime(complication->text, MAX_TEXT_SIZE, "%m", localtime(&now));
            break;
        }
        case ComplicationType_Steps: {
            const uint32_t val = get_steps_value(complication->value == 1);
            if (val < 10000) {  // kilo-units, 1 decimal place
                const int len = snprintf(complication->text, MAX_TEXT_SIZE, "%u.%uk",
                                         (uint8_t)(val / 1000), (uint8_t)((val % 1000) / 100));
                ASSERT(len == 4);  // suppress GCC -Wformat-truncation
            } else {  // kilo-units, 0 decimal places
                const int len = snprintf(complication->text, MAX_TEXT_SIZE, "%uk", (uint8_t)MIN(99, val / 1000));
                ASSERT(len == 3);  // suppress GCC -Wformat-truncation
            }
            break;
        }
        case ComplicationType_Sleep: {
            const time_t seconds = (time_t)get_sleep_seconds(complication->value == 1);
            struct tm * gmtime(const time_t * timer);
            const int len = snprintf(complication->text, MAX_TEXT_SIZE, "%uh%u%u",
                (uint8_t)(seconds / SECONDS_PER_HOUR),
                (uint8_t)(((seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE) / 10),
                (uint8_t)(((seconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE) % 10)
            );
            ASSERT((len == 4) || (len == 5));
            break;
        }
        case ComplicationType_Timezone: {
            // The timezone text suffix is written to complication->text by complications_configure()
            // so insert the hour digits before it.
            const int8_t utc_offset = complication->value - 'M';  // See how values are assigned in config.json
            const time_t now_offset = now + (utc_offset * SECONDS_PER_HOUR);
            const int hour = gmtime(&now_offset)->tm_hour;
            complication->text[0] = '0' + (hour / 10);
            complication->text[1] = '0' + (hour % 10);
            break;
        }
        default: {
            ASSERT(false);
            break;
        }
    }
}

#define MAX_COMPLICATION_SIZE GSize(68, 35)
static void update_complication_size(Complication* complication) {
    switch (complication->type) {
        case ComplicationType_Timezone:
            complication->rect.size = (GSize){65, 25};  // TODO optimise
            break;
        case ComplicationType_Sleep:
            complication->rect.size = (GSize){68, 25};
            break;
        case ComplicationType_Battery:
        case ComplicationType_DayNum:
        case ComplicationType_MonthNum:
        case ComplicationType_Steps:
            complication->rect.size = (GSize){53, 25};
            break;
        case ComplicationType_DayName:
        case ComplicationType_MonthName:
            complication->rect.size = (GSize){56, 22};
            break;
        case ComplicationType_Tick:
        case ComplicationType_Menacing:
            complication->rect = gbitmap_get_bounds(complication->bitmap);  // note they are both 42 * 35
            break;
        default:
            ASSERT(false);
            break;
    }

    ASSERT(complication->rect.size.w <= MAX_COMPLICATION_SIZE.w);
    ASSERT(complication->rect.size.h <= MAX_COMPLICATION_SIZE.h);
}

static bool is_other_drawn_before_complication(const Complication* complication, const Complication* other) {
    return (
        (
            (complication->mode == ComplicationMode_OnActivate) && (
                (other->mode == ComplicationMode_OnAlways)
                || ((other->mode == ComplicationMode_OnActivate) && (other->index < complication->index))
            )
        ) || (
            (complication->mode == ComplicationMode_OnAlways) && (
                (other->mode == ComplicationMode_OnAlways) && (other->index < complication->index)
            )
        )
    );
}

// Find the `other` complication that would be initialised and drawn before `complication`.
// Or NULL if this is the first one.
// Comps are drawn from 0 to MAX_COMPLICATIONS,
// except all "OnAlways" comps are drawn before "OnActivate" Comps
static const Complication* get_previously_drawn_complication(const Complication* complication) {
    for (int32_t i = (NUM_COMPLICATIONS - 1); i >= 0; i--) {
        const Complication* const other = &s_complications[i];
        if (is_other_drawn_before_complication(complication, other)) {
            return other;
        }
    }
    return NULL;
}

// how many segments to divide the clockface into for fallback gap detection
#define GAP_NUM_SEGMENTS (60)

// Populate fallback gap array with the current segments containing hands or complications
// WONTDO Ignore deactivated complications
static void populate_gap_array(bool is_segment_full[GAP_NUM_SEGMENTS]) {
    // This function assumes the first two complications have already been placed
    // because there is always enough space for two
    int32_t full_angles[5];

    // Find the last complication to be drawn, which must be the one needing placement here.
    const Complication* final_comp = &s_complications[NUM_COMPLICATIONS - 1];
    for (int32_t i = (NUM_COMPLICATIONS - 1); i >= 0; i--){
        const Complication* complication = &s_complications[i];
        if (complication->mode == ComplicationMode_OnActivate) {
            final_comp = complication;
            break;
        }
    }
    const Complication* prev_comp = get_previously_drawn_complication(final_comp);
    ASSERT(prev_comp != NULL);
    const Complication* prev_prev_comp = get_previously_drawn_complication(prev_comp);
    ASSERT(prev_prev_comp != NULL);

    const HandAngles hand_angles = analogue_get_angles();
    full_angles[0] = hand_angles.sec - SEC_TO_TRIGANGLE(1);  // assume this is the third tick
    full_angles[1] = hand_angles.min;
    full_angles[2] = hand_angles.hour;
    full_angles[3] = prev_comp->angle;
    full_angles[4] = prev_prev_comp->angle;

    // how many segments each object extends either side of its centre (approximate)
    // (must match the order assigned to full_angles above)
    const int32_t half_arc[5] = {
        1, // second hand
        1, // minute hand
        1, // hour hand
        3, // complication
        3, // complication
    };

    for (uint32_t i = 0; i < ARRAY_LEN(full_angles); i++) {
        const int32_t segment = MUL_FRACT(full_angles[i], GAP_NUM_SEGMENTS, TRIG_MAX_ANGLE);
        is_segment_full[segment] = true;
        is_segment_full[(segment + half_arc[i]) % GAP_NUM_SEGMENTS] = true;
        is_segment_full[(segment - half_arc[i]) % GAP_NUM_SEGMENTS] = true;
    }
}

/// Fallback algorithm to place a complication when we've failed to calculate a
/// position that definitely won't overlap
static int32_t find_biggest_gap(void) {
    bool is_segment_full[GAP_NUM_SEGMENTS] = {false};
    populate_gap_array(is_segment_full);

    // find the beginning of a gap to start from
    int32_t start = 0;
    while (!is_segment_full[start] && (start < GAP_NUM_SEGMENTS)) {
        start++;
    }
    while (is_segment_full[start] && (start < GAP_NUM_SEGMENTS)) {
        start++;
    }
    ASSERT(start != 0);
    // this breaks if there's only one gap, but it shouldn't be possible to be here in that case.
    ASSERT(start < (GAP_NUM_SEGMENTS - 1));

    int32_t largest_gap_centre_index = -1;
    int32_t largest_gap_size = 1;
    int32_t current_gap_size = 1;

    LOG("gap search start = %d", start);
    for (int32_t i = (start + 1); i != start; i = (i + 1) % GAP_NUM_SEGMENTS) {
        if (!is_segment_full[i]) {
            current_gap_size++;
        } else {
            if (current_gap_size > largest_gap_size) {
                LOG("new best gap at %d", i);
                largest_gap_size = current_gap_size;
                largest_gap_centre_index = (i - (largest_gap_size / 2)) % GAP_NUM_SEGMENTS;
            }
            current_gap_size = 0;
        }
    }

    LOG("best gap centre = %d", largest_gap_centre_index);
    return MUL_FRACT(largest_gap_centre_index, TRIG_MAX_ANGLE, GAP_NUM_SEGMENTS);
}

// Set the initial position of a complication
static void update_complication_position(Complication* complication, bool avoid_second_hand) {
    const uint32_t index = complication->index;
    const GPoint display_centre = {PBL_DISPLAY_WIDTH / 2, PBL_DISPLAY_WIDTH / 2};

    // Initial placement depends on its lifetime boundaries
    // For "always on" comps with no movement, still pretend it will float up a bit
    // to give a similar amount of space around them
    const bool is_on_activate = (complication->mode == ComplicationMode_OnActivate);
    const uint32_t float_dist_index = is_on_activate ? index : (NUM_COMPLICATIONS - 1);
    const int16_t floatup_dist = floatup_distance(float_dist_index);
    const GSize total_size = lifetime_rect(complication->rect, float_dist_index).size;

    // Start searching from the previous complication's angle
    // TODO starting from a fixed angle can waste some space; start from the hour hand? start from a random hand?
    // WONTDO handle comp2 disabled but comp1 and comp3 enabled
    int32_t start_angle_deg = 0;
    const Complication* const previous_comp = get_previously_drawn_complication(complication);
    if (previous_comp == NULL) {
        if (config_get()->batteryMeterEnabled) {
            start_angle_deg = is_on_activate ? 90 : 55;
        }
    } else {
        start_angle_deg = TRIGANGLE_TO_DEG(previous_comp->angle + MIN_TO_TRIGANGLE(4)) % 360;
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
        if ((avoid_second_hand
             && (ABSDIFF_WRAP(arc_centre_angle, second_centre, TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(2.5) + arc) / 2))
            )
         || (ABSDIFF_WRAP(arc_centre_angle, hand_angles.min,  TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(3.5) + arc) / 2))
         || (ABSDIFF_WRAP(arc_centre_angle, hand_angles.hour, TRIG_MAX_ANGLE) < ((MIN_TO_TRIGANGLE(5.5) + arc) / 2))
        ) {
            continue;
        }

        // Check for collision with other complications
        bool overlaps_other_complication = false;
        // TODO get closer by taking into account the floating movement
        for (uint32_t i = 0; i < NUM_COMPLICATIONS; i++) {
            const Complication* const other = &s_complications[i];
            if (is_other_drawn_before_complication(complication, other)) {
                if (ABSDIFF_WRAP(arc_centre_angle, other->angle, TRIG_MAX_ANGLE)
                        < (((other->arc + arc) / 2) - DEG_TO_TRIGANGLE(15)))
                {
                    overlaps_other_complication = true;
                    break;
                }
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
        return;
    }

    // This should only be possible for the last complication
    LOG_ERR("Failed to place complication #%u", index);

    // Fallback; just put it in the biggest gap.
    complication->angle = find_biggest_gap();
    complication->arc = 0; // don't bother setting it correctly; only used for the previous angle selection algorithm
    const GPoint bitmap_centre = point_from_angle(display_centre, complication->angle, COMPLICATION_RADIUS);
    complication->rect.origin = GPoint(
        bitmap_centre.x - total_size.w / 2,
        bitmap_centre.y - total_size.h / 2
    );
    complication->rect.origin.y += floatup_dist;
}

// Initialise or update a complication, and start showing it
static void complication_start(uint32_t index, bool avoid_second_hand) {
    ASSERT(index < NUM_COMPLICATIONS);
    Complication* const complication = &s_complications[index];

    update_complication_display_value(complication);
    update_complication_size(complication);
    update_complication_position(complication, avoid_second_hand);

    if (complication->mode == ComplicationMode_OnActivate) {
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
    }
}

// Wrapper to call complication_start() from an AppTimer
/// WARNING arg `context` is an int complication index, not a pointer
static void complication_start_callback(void* context) {
    uint32_t complication_index = (uint32_t)context;
    complication_start(complication_index, true);
}

void complication_start_next(void) {
    const AnimationSpeed anim_speed = config_get()->animationSpeed;
    uint32_t fastAnimDelayMs = 0;

    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        Complication* const complication = &s_complications[i];
        if ((complication->mode == ComplicationMode_OnActivate) && (complication->state == ComplicationState_Off)) {
            if (fastAnimDelayMs == 0) {
                complication_start(i, true);
            }
            if (anim_speed == AnimationSpeed_Normal) {
                break;
            } else if (anim_speed == AnimationSpeed_Fast) {
                fastAnimDelayMs += 100;
                app_timer_register(fastAnimDelayMs, complication_start_callback, (void*)i);
            }
        }
    }
}

// Update any always-on complications; intended to be called on each minute tick
void complications_update(void) {
    for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        Complication* const complication = &s_complications[i];
        if (complication->mode == ComplicationMode_OnAlways) {
            complication_start(i, false);
        }
    }
}

void complications_stop(void) {
    // assumes all complications are already on
    if (config_get()->animationSpeed == AnimationSpeed_Off) {
        for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
            s_complications[i].state = ComplicationState_Off;
        }
    } else {
        uint32_t delay = 0;
        for (int32_t i = 0; i < NUM_COMPLICATIONS; i++) {
            s_complications[i].animation_counter = delay;
            delay += FRAMERATE / 10;
        }
        // the last complication is still in State_Appear
        s_complications[NUM_COMPLICATIONS - 1].state = ComplicationState_On;
        s_complications[NUM_COMPLICATIONS - 1].shake_offset = (GPoint) {0, 0};
    }
}

void complications_configure(const Config* config){
    s_complications[0].mode = config->comp1Mode;
    s_complications[1].mode = config->comp2Mode;
    s_complications[2].mode = config->comp3Mode;
    s_complications[0].type = config->comp1Type;
    s_complications[1].type = config->comp2Type;
    s_complications[2].type = config->comp3Type;
    s_complications[0].color1 = config->comp1FgColor;
    s_complications[1].color1 = config->comp2FgColor;
    s_complications[2].color1 = config->comp3FgColor;
    s_complications[0].color2 = config->comp1BgColor;
    s_complications[1].color2 = config->comp2BgColor;
    s_complications[2].color2 = config->comp3BgColor;
    s_complications[0].value = config->comp1Value;
    s_complications[1].value = config->comp2Value;
    s_complications[2].value = config->comp3Value;

    STATIC_ASSERT(TZ_TEXT_SIZE <= MAX_TEXT_SIZE);
    strscpy(&s_complications[0].text[2], config->comp1TimezoneText, TZ_TEXT_SIZE);
    strscpy(&s_complications[1].text[2], config->comp2TimezoneText, TZ_TEXT_SIZE);
    strscpy(&s_complications[2].text[2], config->comp3TimezoneText, TZ_TEXT_SIZE);
    string_to_lower(&s_complications[0].text[2], TZ_TEXT_SIZE);
    string_to_lower(&s_complications[1].text[2], TZ_TEXT_SIZE);
    string_to_lower(&s_complications[2].text[2], TZ_TEXT_SIZE);
}

void complications_init(void) {
    s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_MANGA_22));
    s_tick = gbitmap_create_with_resource(RESOURCE_ID_TICK);
    s_go = gbitmap_create_with_resource(RESOURCE_ID_GO);

    for (uint32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        s_complications[i].index = i;

        const GSize bgbuffer_size = lifetime_rect((GRect){{0, 0}, MAX_COMPLICATION_SIZE}, NUM_COMPLICATIONS - 1).size;
        // TODO support other framebuffer types
        s_complications[i].bgbuffer = gbitmap_create_blank(bgbuffer_size, GBitmapFormat8Bit);
    }
}

void complications_destroy(void) {
    fonts_unload_custom_font(s_font);
    gbitmap_destroy(s_tick);
    gbitmap_destroy(s_go);

    for (uint32_t i = 0; i < NUM_COMPLICATIONS; i++) {
        gbitmap_destroy(s_complications[i].bgbuffer);
    }
}
