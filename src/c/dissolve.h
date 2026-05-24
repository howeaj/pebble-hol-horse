// Copyright (c) 2026 Andrew Howe. All rights reserved. See LICENSE (GPLv3.0).

#pragma once

#include <pebble.h>

#include "debug.h"
#include "macros.h"

size_t gbitmap_get_size(const GBitmap* bitmap);
void dissolve(GBitmap *bitmap);
