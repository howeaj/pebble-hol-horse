// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include "config.h"

#include <pebble.h>

#define BGCOLOR (GColorWhite)
#define NUM_COMPLICATIONS (3)


void complications_draw(Layer* layer, GContext* ctx, bool is_full_redraw);
void complication_start_next(void);
void complications_update(void);
void complications_stop(void);
void complications_configure(const Config* config);
void complications_init(void);
void complications_destroy(void);
