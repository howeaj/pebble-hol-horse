// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#define BGCOLOR (GColorWhite)
#define NUM_COMPLICATIONS (3)

void complication_draw(Layer* layer, GContext* ctx, uint32_t index, bool is_full_redraw);
void complication_start(uint32_t index);
void complications_stop(void);
void complications_init(void);
void complications_destroy(void);
