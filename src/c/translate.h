// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#include "debug.h"
#include "macros.h"

typedef struct GVector {
  int16_t dx;
  int16_t dy;
} GVector;
#define GVector(dx, dy) ((GVector){(dx), (dy)})

void translate_rect(GContext* ctx, GRect rect, GVector vector, const GColor* colorMask, size_t colorMaskLen);